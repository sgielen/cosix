#include "hw/net/virtio.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "memory/page_allocator.hpp"
#include "oslibc/error.h"
#include "oslibc/string.h"
#include "oslibc/checksum.h"

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

#define PAGE_SIZE 4096

namespace cloudos {

struct virtq_buffer {
	/* Address (guest-physical). */
	uint64_t addr;
	/* Length. */
	uint32_t len;

/* This marks a buffer as continuing via the next field. */
//#define VIRTQ_DESC_F_NEXT   1
/* This marks a buffer as device write-only (otherwise device read-only). */
#define VIRTQ_DESC_F_WRITE     2
/* This means the buffer contains a list of buffer descriptors. */
//#define VIRTQ_DESC_F_INDIRECT   4
	/* The flags as indicated above. */
	uint16_t flags;
	/* Next field if flags & NEXT */
	uint16_t next;
};

struct virtq_avail {
//#define VIRTQ_AVAIL_F_NO_INTERRUPT      1
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
	/* an additional uint16_t here if VIRTIO_F_EVENT_IDX is set */
} __attribute__((__packed__));

struct virtq_used {
//#define VIRTQ_USED_F_NO_NOTIFY  1
	uint16_t flags;
	uint16_t idx;
	struct {
		/* Index of start of used descriptor chain. */
		uint32_t id;
		/* Total length of the descriptor chain which was used (written to) */
		uint32_t len;
	} ring[];
	/* an additional uint16_t here if VIRTIO_F_EVENT_IDX is set */
} __attribute__((__packed__));

struct virtq {
	virtq(uint16_t q, uint32_t s)
	: data(0), queue_select(q), queue_size(s), padding_used(0) {
		/* Each virtq occupies two or more physically-contiguous pages (usually defined as
		   4096 bytes, but depending on the transport; henceforth referred to as Queue Align) */
		size_t iovirtq_bytes = 0;
		/* the ring buffers */
		iovirtq_bytes += sizeof(virtq_buffer) * queue_size;
		/* availability list */
		iovirtq_bytes += 2 + queue_size * 2;
		/* pad to the next page */
		padding_used = PAGE_SIZE - (iovirtq_bytes % PAGE_SIZE);
		iovirtq_bytes += padding_used;
		/* used list */
		iovirtq_bytes += 2 + queue_size * 8;
		data = reinterpret_cast<uint8_t*>(get_map_virtual()->allocate_contiguous_phys(iovirtq_bytes).ptr);
		memset(data, 0, iovirtq_bytes);
	}

	uint16_t get_queue_select() {
		return queue_select;
	}

	uint16_t get_queue_size() {
		return queue_size;
	}

	void *get_virtq_addr_phys() {
		return get_map_virtual()->to_physical_address(data);
	}

	virtq_buffer *get_virtq_buffer(int i) {
		if(i >= queue_size) {
			kernel_panic("get_virtq_buffer: buffer index too high");
		}
		auto buf = reinterpret_cast<virtq_buffer*>(data);
		return buf + i;
	}

	virtq_avail *get_virtq_avail() {
		return reinterpret_cast<virtq_avail*>(data + sizeof(virtq_buffer) * queue_size);
	}

	uint8_t *get_padding() {
		return data + 2 + (sizeof(virtq_buffer) + 2) * queue_size;
	}

	virtq_used *get_virtq_used() {
		return reinterpret_cast<virtq_used*>(data + 2 + (sizeof(virtq_buffer) + 2) * queue_size + padding_used);
	}

private:
	uint8_t *data;
	uint16_t queue_select;
	uint16_t queue_size;
	uint32_t padding_used;
};
}

const uint64_t VIRTIO_NET_F_CSUM = 1 << 0; // Checksum offloading
const uint64_t VIRTIO_NET_F_MAC = 1 << 5; // Device has given MAC address
const uint64_t VIRTIO_NET_F_MRG_RXBUF = 1 << 15; // Guest can merge receive buffers
const uint64_t VIRTIO_NET_F_STATUS = 1 << 16; // Configuration status field is available
const uint64_t VIRTIO_F_NOTIFY_ON_EMPTY = 1 << 24;

device *virtio_net_driver::probe_pci_device(pci_bus *bus, int device)
{
	// VirtIO vendor
	if(bus->get_vendor_id(device) != 0x1af4) {
		return nullptr;
	}

	// VirtIO legacy device
	uint16_t device_id = bus->get_device_id(device);
	if(device_id < 0x1000 || device_id > 0x103f) {
		return nullptr;
	}

	// VirtIO NIC
	if(bus->get_subsystem_id(device) != 0x01) {
		return nullptr;
	}

	auto *res = get_allocator()->allocate<virtio_net_device>();
	new(res) virtio_net_device(bus, device);
	return res;
}

const char *virtio_net_driver::description()
{
	return "VirtIO legacy network card driver";
}

const char *virtio_net_device::description()
{
	return "VirtIO legacy network card";
}

virtio_net_device::virtio_net_device(pci_bus *parent, int d)
: device(parent)
, pci_device(parent, d)
, last_readq_used_idx(0)
, last_writeq_idx(0)
, readq(nullptr)
, writeq(nullptr)
, mappings(0)
{
}

cloudabi_errno_t virtio_net_device::add_buffer_to_avail(virtq *queue, int buffer_id) {
	auto *avail = queue->get_virtq_avail();
	avail->ring[avail->idx % queue->get_queue_size()] = buffer_id;

	// memory barrier
	asm volatile ("": : :"memory");

	avail->idx += 1;

	// memory barrier
	asm volatile ("": : :"memory");

	write32(0x10, queue->get_queue_select());

	return 0;
}

cloudabi_errno_t virtio_net_device::eth_init()
{
	//uint8_t int_info = read_pci_config(bus, device, 0, 0x3c) & 0xff;
	//get_vga_stream() << "Will use IRQ " << int_info << "\n";
	uint32_t const device_features = 0x00; // 4 bytes
	uint32_t const driver_features = 0x04; // 4 bytes
	uint32_t const queue_address = 0x08; // 4 bytes
	uint32_t const queue_size = 0x0c; // 2 bytes
	uint32_t const queue_select = 0x0e; // 2 bytes
	uint32_t const device_status = 0x12; // 1 byte

	// acknowledge device
	write8(device_status, 0); /* reset device */
	write8(device_status, 1); /* device acknowledged */
	write8(device_status, 3); /* driver available */

	uint64_t sup_features = read32(device_features);
	sup_features |= uint64_t(read32(device_features + 1)) << 32;
	get_vga_stream() << "Device features: 0x" << hex << sup_features << dec << "\n";

	if((sup_features & VIRTIO_NET_F_MAC) == 0) {
		// TODO: if the VIRTIO_NET_F_MAC feature is not in
		// device_features, we must randomly generate our own MAC
		get_vga_stream() << "MAC setting not supported. Skipping device.\n";
		write8(device_status, 128); /* driver failed */
		return ENOTSUP;
	}

	if((sup_features & VIRTIO_NET_F_STATUS) == 0) {
		get_vga_stream() << "Device status bit not supported. Skipping device.\n";
		write8(device_status, 128); /* driver failed */
		return ENOTSUP;
	}

	if((sup_features & VIRTIO_NET_F_MRG_RXBUF) == 0) {
		get_vga_stream() << "Merging received buffers not supported. Skipping device.\n";
		write8(device_status, 128); /* driver failed */
		return ENOTSUP;
	}

	if((sup_features & VIRTIO_F_NOTIFY_ON_EMPTY) == 0) {
		get_vga_stream() << "Notify on empty not supported. Skipping device.\n";
		write8(device_status, 128); /* driver failed */
		return ENOTSUP;
	}

	drv_features = sup_features & (VIRTIO_NET_F_MAC
			| VIRTIO_NET_F_STATUS
			| VIRTIO_NET_F_CSUM
			| VIRTIO_NET_F_MRG_RXBUF
			| VIRTIO_F_NOTIFY_ON_EMPTY);
	write32(driver_features, drv_features & 0xffffffff);
	write32(driver_features + 1, drv_features >> 32);
	get_vga_stream() << "Driver features: 0x" << hex << read32(driver_features) << dec << "\n";

	write8(device_status, 11); /* features OK */

	mac[0] = read8(0x14);
	mac[1] = read8(0x15);
	mac[2] = read8(0x16);
	mac[3] = read8(0x17);
	mac[4] = read8(0x18);
	mac[5] = read8(0x19);

	get_vga_stream() << "MAC address: " << hex << mac[0] << ":" << mac[1] << ":" << mac[2] << ":" << mac[3] << ":" << mac[4] << ":" << mac[5] << dec << "\n";

	for(int queue = 0; queue < 2; ++queue) {
		write16(queue_select, queue);
		auto number_of_entries = read16(queue_size);

		virtq *q = get_allocator()->allocate<virtq>();
		new(q) virtq(queue, number_of_entries);

		uint64_t virtq_addr_phys = reinterpret_cast<uint64_t>(q->get_virtq_addr_phys());
		if((virtq_addr_phys % PAGE_SIZE) != 0) {
			get_vga_stream() << "Failed to allocate descriptor table aligned to a page\n";
			return ENOMEM;
		}
		write32(queue_address, virtq_addr_phys >> 12);
		(queue == 0 ? readq : writeq) = q;
	}

	// make some buffers available -- the more buffers we allocate,
	// the more data the virtio NIC can feed us between processing, but the
	// more memory we're using.
	auto avail = readq->get_virtq_avail();
	avail->flags = 0;
	for(int i = 0; i < 20; ++i) {
		auto buffer = readq->get_virtq_buffer(i);

		// TODO: use MTU instead of fixed size
		buffer->len = PAGE_SIZE;
		void *address = get_map_virtual()->allocate(PAGE_SIZE).ptr;
		buffer->addr = reinterpret_cast<uint64_t>(get_map_virtual()->to_physical_address(address));
		buffer->flags = VIRTQ_DESC_F_WRITE;
		buffer->next = 0;

		address_mapping *mapping = get_allocator()->allocate<address_mapping>();
		address_mapping_list *mappingl = get_allocator()->allocate<address_mapping_list>();
		mapping->logical = address;
		mapping->physical = reinterpret_cast<void*>(buffer->addr);
		mappingl->data = mapping;
		mappingl->next = nullptr;
		append(&mappings, mappingl);

		auto res = add_buffer_to_avail(readq, i);
		if(res != 0) {
			kernel_panic("Failed to add initial buffers to virtio NIC");
		}
	}

	write8(device_status, 15); /* driver ready */
	return 0;
}

cloudabi_errno_t virtio_net_device::check_new_packets() {
	if(readq == nullptr) {
		return EINVAL;
	}

	auto *virtq_used = readq->get_virtq_used();
	while(virtq_used->idx > last_readq_used_idx) {
		uint32_t buffer_id = virtq_used->ring[last_readq_used_idx % readq->get_queue_size()].id;
		uint32_t length = virtq_used->ring[last_readq_used_idx % readq->get_queue_size()].len;
		auto *buffer = readq->get_virtq_buffer(buffer_id);

		uint32_t nethdr_phys = buffer->addr & 0xffffffff;

		address_mapping_list *found = find(mappings, [nethdr_phys](address_mapping_list *item) {
			return reinterpret_cast<uint32_t>(item->data->physical) == nethdr_phys;
		});
		if(found == nullptr) {
			kernel_panic("Did not find logical address for physical kernel address of nethdr");
		}
		uint8_t *nethdr = reinterpret_cast<uint8_t*>(found->data->logical);
		assert(reinterpret_cast<uint32_t>(nethdr) >= _kernel_virtual_base);
		auto res = ethernet_frame_received(nethdr + 12, length - 12);

		last_readq_used_idx++;
		auto res2 = add_buffer_to_avail(readq, buffer_id);

		if(res != 0) {
			return res;
		}
		if(res2 != 0) {
			return res2;
		}
	}

	return 0;
}

cloudabi_errno_t virtio_net_device::get_mac_address(char m[6]) {
	if(readq == nullptr) {
		return EINVAL;
	}

	memcpy(m, mac, 6);
	return 0;
}

cloudabi_errno_t virtio_net_device::send_ethernet_frame(uint8_t *frame, size_t length) {
	struct virtio_net_hdr {
		uint8_t flags;
		uint8_t gso_type;
		uint16_t hdr_len;
		uint16_t gso_size;
		uint16_t csum_start;
		uint16_t csum_off;
		uint16_t buff_count;
	};
	virtio_net_hdr net_hdr;
	memset(&net_hdr, 0, sizeof(net_hdr));

	// Length contains 4 bytes left over for an Ethernet CRC
	if(0 /* untested */ && drv_features & VIRTIO_NET_F_CSUM) {
		// checksum offloading is supported
		net_hdr.flags = 1; // needs checksum
		net_hdr.csum_start = 0;
		net_hdr.csum_off = length - 4;
	} else {
		uint32_t *checksum = reinterpret_cast<uint32_t*>(frame + (length - 4));
		*checksum = crc_32(frame, length - 4);
	}

	size_t desc_idx = last_writeq_idx++;

	if(last_writeq_idx >= writeq->get_queue_size()) {
		// TODO: instead of adding our buffers to the virtio NIC constantly,
		// allocate some actual write buffers in the constructor and cycle
		// through their indices in the writeq like we do for the readq
		// --- or, alternatively, reuse buffer indices from earlier sent
		// packets that aren't used anymore, so we don't have to copy into
		// send buffers.
		kernel_panic("Trying to write more buffers to virtio device than fit in the queue, fix the virtio driver");
	}

	auto desc = writeq->get_virtq_buffer(desc_idx);

	size_t total_length = sizeof(net_hdr) + length;
	if(total_length > PAGE_SIZE) {
		kernel_panic("Trying to send packet larger than page size");
	}

	// TODO: use MTU instead of fixed size
	// TODO: fix memory leaks
	auto alloc = get_map_virtual()->allocate(PAGE_SIZE);
	char *address = reinterpret_cast<char*>(alloc.ptr);

	memcpy(address, &net_hdr, sizeof(net_hdr));
	memcpy(address + sizeof(net_hdr), frame, length);

	address_mapping *mapping = allocate<address_mapping>();
	mapping->logical = address;
	mapping->physical = get_map_virtual()->to_physical_address(address);

	address_mapping_list *item = allocate<address_mapping_list>(mapping);
	append(&mappings, item);

	desc->addr = reinterpret_cast<uint64_t>(mapping->physical);
	desc->len = total_length;
	desc->flags = 0;
	desc->next = 0;

	auto avail = writeq->get_virtq_avail();
	avail->flags = 0;
	auto res = add_buffer_to_avail(writeq, desc_idx);
	return res;
}

void virtio_net_device::timer_event() {
	// TODO: do this when the correct interrupt arrives, instead of
	// when the timer fires
	auto res = check_new_packets();
	if(res != 0) {
		get_vga_stream() << "check_new_packets failed: " << res << "\n";
	}
}
