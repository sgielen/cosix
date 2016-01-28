#include "hw/net/virtio.hpp"
#include "hw/cpu_io.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "oslibc/error.h"

using namespace cloudos;

namespace cloudos {
struct virtq_desc {
	/* Address (guest-physical). */
	uint64_t addr;
	/* Length. */
	uint32_t len;

/* This marks a buffer as continuing via the next field. */
#define VIRTQ_DESC_F_NEXT   1
/* This marks a buffer as device write-only (otherwise device read-only). */
#define VIRTQ_DESC_F_WRITE     2
/* This means the buffer contains a list of buffer descriptors. */
#define VIRTQ_DESC_F_INDIRECT   4
	/* The flags as indicated above. */
	uint16_t flags;
	/* Next field if flags & NEXT */
	uint16_t next;
};

struct virtq_avail {
#define VIRTQ_AVAIL_F_NO_INTERRUPT      1
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
	/* an additional uint16_t here if VIRTIO_F_EVENT_IDX is set */
} __attribute__((__packed__));

struct virtq_used {
#define VIRTQ_USED_F_NO_NOTIFY  1
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
		/* the ring descriptors */
		iovirtq_bytes += sizeof(virtq_desc) * queue_size;
		/* availability list */
		iovirtq_bytes += 2 + queue_size * 2;
		/* pad to the next page */
		padding_used = 4096 - (iovirtq_bytes % 4096);
		iovirtq_bytes += padding_used;
		/* used list */
		iovirtq_bytes += 2 + queue_size * 8;
		/* TODO: this must be DMA physical memory */
		data = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(iovirtq_bytes, 4096));
		// initialize to zero
		for(size_t i = 0; i < iovirtq_bytes; ++i) {
			data[i] = 0;
		}
	}

	uint16_t get_queue_select() {
		return queue_select;
	}

	uint16_t get_queue_size() {
		return queue_size;
	}

	void *get_virtq_addr() {
		return reinterpret_cast<void*>(data);
	}

	virtq_desc *get_virtq_desc(int i) {
		/* TODO: assertion */
		if(i >= queue_size) {
			return NULL;
		}
		auto iovirtdesc = reinterpret_cast<virtq_desc*>(data);
		return iovirtdesc + i;
	}

	virtq_avail *get_virtq_avail() {
		return reinterpret_cast<virtq_avail*>(data + sizeof(virtq_desc) * queue_size);
	}

	uint8_t *get_padding() {
		return data + 2 + (sizeof(virtq_desc) + 2) * queue_size;
	}

	virtq_used *get_virtq_used() {
		return reinterpret_cast<virtq_used*>(data + 2 + (sizeof(virtq_desc) + 2) * queue_size + padding_used);
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
, bus(parent)
, bus_device(d)
, last_readq_idx(0)
, last_writeq_idx(0)
, readq(nullptr)
, writeq(nullptr)
{
}

error_t virtio_net_device::init()
{
	get_vga_stream() << "Found VirtIO NIC. Initializing it.\n";
	uint32_t bar0 = bus->get_bar0(bus_device);
	if((bar0 & 0x01) != 1) {
		get_vga_stream() << "Not I/O space BAR type, skipping.\n";
		return error_t::dev_not_supported;
	}
	bar0 = bar0 & 0xfffffffc;
	get_vga_stream() << "bar0: 0x" << hex << bar0 << dec << "\n";
	//uint8_t int_info = read_pci_config(bus, device, 0, 0x3c) & 0xff;
	//get_vga_stream() << "Will use IRQ " << int_info << "\n";
	uint32_t mem_base = bar0;
	uint32_t const device_features = mem_base + 0x00; // 4 bytes
	uint32_t const driver_features = mem_base + 0x04; // 4 bytes
	uint32_t const queue_address = mem_base + 0x08; // 4 bytes
	uint32_t const queue_size = mem_base + 0x0c; // 2 bytes
	uint32_t const queue_select = mem_base + 0x0e; // 2 bytes
	uint32_t const queue_notify = mem_base + 0x10; // 2 bytes
	uint32_t const device_status = mem_base + 0x12; // 1 byte
	uint32_t const isr_status = mem_base + 0x13; // 1 byte

	// acknowledge device
	outb(device_status, 0); /* reset device */
	outb(device_status, 1); /* device acknowledged */
	outb(device_status, 2); /* driver available */

	uint64_t sup_features = inl(device_features);
	sup_features |= uint64_t(inl(device_features + 1)) << 32;
	get_vga_stream() << "Device features: 0x" << hex << sup_features << dec << "\n";

	if((sup_features & VIRTIO_NET_F_MAC) == 0) {
		// TODO: if the VIRTIO_NET_F_MAC feature is not in
		// device_features, we must randomly generate our own MAC
		get_vga_stream() << "MAC setting not supported. Skipping device.\n";
		outb(device_status, 128); /* driver failed */
		return error_t::dev_not_supported;
	}

	if((sup_features & VIRTIO_NET_F_STATUS) == 0) {
		get_vga_stream() << "Device status bit not supported. Skipping device.\n";
		outb(device_status, 128); /* driver failed */
		return error_t::dev_not_supported;
	}

	if((sup_features & VIRTIO_NET_F_MRG_RXBUF) == 0) {
		get_vga_stream() << "Merging received buffers not supported. Skipping device.\n";
		outb(device_status, 128); /* driver failed */
		return error_t::dev_not_supported;
	}

	if((sup_features & VIRTIO_F_NOTIFY_ON_EMPTY) == 0) {
		get_vga_stream() << "Notify on empty not supported. Skipping device.\n";
		outb(device_status, 128); /* driver failed */
		return error_t::dev_not_supported;
	}

	uint64_t drv_features = sup_features & (VIRTIO_NET_F_MAC
			| VIRTIO_NET_F_STATUS
			| VIRTIO_NET_F_CSUM
			| VIRTIO_NET_F_MRG_RXBUF
			| VIRTIO_F_NOTIFY_ON_EMPTY);
	outl(driver_features, drv_features & 0xffffffff);
	outl(driver_features + 1, drv_features >> 32);
	get_vga_stream() << "Driver features: 0x" << hex << inl(driver_features) << dec << "\n";

	uint8_t mac[6];
	{
		mac[0] = inb(mem_base + 0x14);
		mac[1] = inb(mem_base + 0x15);
		mac[2] = inb(mem_base + 0x16);
		mac[3] = inb(mem_base + 0x17);
		mac[4] = inb(mem_base + 0x18);
		mac[5] = inb(mem_base + 0x19);
	}
	get_vga_stream() << "MAC address: " << hex << mac[0] << ":" << mac[1] << ":" << mac[2] << ":" << mac[3] << ":" << mac[4] << ":" << mac[5] << "\n";

	for(int queue = 0; queue < 2; ++queue) {
		outw(queue_select, queue);
		auto number_of_entries = inw(queue_size);

		virtq *q = get_allocator()->allocate<virtq>();
		new(q) virtq(queue, number_of_entries);

		get_vga_stream() << "Data ptr: " << hex << q->get_virtq_addr() << dec << "\n";
		if((reinterpret_cast<uint64_t>(q->get_virtq_addr()) % 4096) != 0) {
			get_vga_stream() << "Failed to allocate descriptor table aligned to 4096 bytes\n";
			return error_t::no_memory;
		}
		outl(queue_address, reinterpret_cast<uint64_t>(q->get_virtq_addr()) / 4096);
		(queue == 0 ? readq : writeq) = q;
	}

	get_vga_stream() << "Readq  queue size: " << readq->get_queue_size() << "\n";
	get_vga_stream() << "Writeq queue size: " << writeq->get_queue_size() << "\n";

	get_vga_stream() << "ISR status: " << inb(isr_status) << "\n";
	get_vga_stream() << "ISR status: " << inb(isr_status) << "\n";

	outb(device_status, 4); /* driver ready */

	// TODO: the code after this is just a test. It sends a DHCP request and prints some information
	// about the resulting response. Also, even though the MAC is filled in dynamically, the checksums
	// are wrong for any MAC other than qemu's default. This code should be replaced with a decent IP
	// stack.

	/* TODO: this must be in DMA phys memory */
	uint8_t net_hdr[] = {
		0x01, /* needs checksum */
		0x00, /* no segmentation */
		0x00, 0x00, /* header length */
		0x00, 0x00, /* segment size */
		0x00, 0x00, /* checksum start */
		0x00, 0x00, /* checksum offset */
		0x00, 0x00, /* buffer count */
	};

	uint8_t bootp[] = {
		/* ethernet */
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // destination MAC
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // source MAC
		0x08, 0x00, // IPv4
		/* ipv4 */
		0x45, 0x00, 0x00, 0xdf, 0xf5, 0xe9, 0x00, 0x00, // id, flags, length, type, ...
		0xff, 0x11, 0xc5, 0x24, // ttl, udp, checksum
		0x00, 0x00, 0x00, 0x00, // source IP
		0xff, 0xff, 0xff, 0xff, // destination IP
		/* udp */
		0x00, 0x44, 0x00, 0x43, // source and dest port
		0x00, 0xcb, 0x00, 0x00, // length and checksum
		/* bootp */
		0x01, 0x01, 0x06, 0x00, // bootp request, ethernet, hw address length 6, 0 hops
		0xd1, 0x4c, 0x52, 0xae, // id
		0x00, 0x00, 0x00, 0x00, // no time elapsed, no flags
		0x00, 0x00, 0x00, 0x00, // client IP
		0x00, 0x00, 0x00, 0x00, // "your" IP
		0x00, 0x00, 0x00, 0x00, // next server IP
		0x00, 0x00, 0x00, 0x00, // relay IP,
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // client MAC (note, this invalidates checksum)
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // hardware address padding
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		/* dhcp */
		0x63, 0x82, 0x53, 0x63, // dhcp magic cookie
		0x35, 0x01, 0x03, // dhcp request
		0x37, 0x0a, 0x01, 0x79, 0x03, 0x06, 0x0f, 0x77, 0xf, 0x5f, 0x2c, 0x2e, // request various other parameters as well
		0x39, 0x02, 0x05, 0xdc, // max response size
		0x3d, 0x07, 0x01, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // client identifier
		/* missing: requested IP address */
		/* missing: IP address lease time */
		0x0c, 0x07, 'c', 'l', 'o', 'u', 'd', 'o', 's', // hostname
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // end + padding
	};

	last_readq_idx = readq->get_virtq_used()->idx;

	/* make some read buffers available */
	{
		auto avail = readq->get_virtq_avail();
		for(int i = 0; i < 10; ++i) {
			auto desc = readq->get_virtq_desc(i);
			desc->addr = reinterpret_cast<uint64_t>(get_allocator()->allocate(2048));
			desc->len = 2048;
			desc->flags = VIRTQ_DESC_F_WRITE;
			desc->next = 0;
			avail->ring[i] = i;
		}
		avail->flags = 0;
		avail->idx = 10;
	}

	auto d0 = writeq->get_virtq_desc(0);
	auto d1 = writeq->get_virtq_desc(1);

	d0->addr = reinterpret_cast<uint64_t>(net_hdr);
	d0->len = sizeof(net_hdr);
	d0->flags = VIRTQ_DESC_F_NEXT;
	d0->next = 1;

	d1->addr = reinterpret_cast<uint64_t>(bootp);
	d1->len = sizeof(bootp);
	d1->flags = 0;
	d1->next = 1;

	// memory barrier
	asm volatile ("": : :"memory");

	auto avail = writeq->get_virtq_avail();
	get_vga_stream() << "Avail ptr: " << hex << avail << dec << "\n";
	avail->flags = 0;
	avail->ring[0] = 0;

	// memory barrier
	asm volatile ("": : :"memory");

	avail->idx = 1;

	// memory barrier
	asm volatile ("": : :"memory");

	outw(queue_notify, writeq->get_queue_select());

	for(int i = 0; i < 100000000; ++i) {
		// memory barrier
		asm volatile ("": : :"memory");
	}

	while(readq->get_virtq_used()->idx > last_readq_idx) {
		uint32_t id = readq->get_virtq_used()->ring[last_readq_idx].id;
		uint32_t len = readq->get_virtq_used()->ring[last_readq_idx].len;
		get_vga_stream() << "New packet with index " << last_readq_idx << ", id " << id << ", len " << len << "\n";
		auto desc = readq->get_virtq_desc(id);
		get_vga_stream() << "Desc addr 0x" << hex << (desc->addr & 0xffffffff) << dec << "\n";
		get_vga_stream() << "     len " << desc->len << ", flags " << desc->flags << ", next " << desc->next << "\n";

		last_readq_idx++;
	}

	return error_t::no_error;
}
