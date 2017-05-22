#include "hw/net/intel_i217.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "memory/page_allocator.hpp"
#include "oslibc/error.h"
#include "oslibc/string.h"
#include "oslibc/checksum.h"
#include "hw/net/intel_i217_flags.hpp"

using namespace cloudos;

namespace cloudos {
struct e1000_rx_desc {
        volatile uint64_t addr;
        volatile uint16_t length;
        volatile uint16_t checksum;
        volatile uint8_t status;
        volatile uint8_t errors;
        volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
        volatile uint64_t addr;
        volatile uint16_t length;
        volatile uint8_t cso;
        volatile uint8_t cmd;
        volatile uint8_t status;
        volatile uint8_t css;
        volatile uint16_t special;
} __attribute__((packed));
}

device *intel_i217_driver::probe_pci_device(pci_bus *bus, int device)
{
	if(bus->get_vendor_id(device) != INTEL_VEND) {
		return nullptr;
	}

	uint16_t device_id = bus->get_device_id(device);
	if(device_id != E1000_DEV && device_id != E1000_I217
	&& device_id != E1000_I217B && device_id != E1000_82577LM) {
		return nullptr;
	}

	return allocate<intel_i217_device>(bus, device);
}

const char *intel_i217_driver::description()
{
	return "Intel i217 network card driver";
}

const char *intel_i217_device::description()
{
	return "Intel i217 network card";
}

intel_i217_device::intel_i217_device(pci_bus *parent, int d)
: device(parent)
, pci_device(parent, d)
{}

intel_i217_device::~intel_i217_device()
{
	get_map_virtual()->deallocate(buffers);
}

uint16_t intel_i217_device::eeprom_read(uint8_t offset)
{
	write32(REG_EEPROM, 1 | (static_cast<uint32_t>(offset) << 8));
	while(true) {
		uint32_t res = read32(REG_EEPROM);
		if(res & (1 << 4)) {
			return (res >> 16) & 0xffff;
		}
	}
}

cloudabi_errno_t intel_i217_device::eth_init()
{
	get_vga_stream() << "Initializing intel i217...\n";

	// Disable bus master
	set_pci_config(0, 0x04, get_pci_config(0, 0x04) & ~(1 << 2));

	// Disable interrupt line
	set_pci_config(0, 0x04, get_pci_config(0, 0x04) & ~(1 << 10));

	// Reset device
	write32(REG_IMC, UINT32_MAX); // TODO: specificaion says upper bits should remain zero
	write32(REG_RCTRL, 0);
	write32(REG_TCTRL, 0);
	write32(REG_CTRL, read32(REG_CTRL) | ECTRL_RST);

	// TODO: should wait for a while here, before reading
	// control register below.
	// TODO: this loop never returns, why not?
	/*while(true) {
		if(read32(REG_CTRL) & ECTRL_PHY_RST) {
			break;
		}
	}*/

	// Disable interrupts again
	write32(REG_IMC, UINT32_MAX); // TODO: specificaion says upper bits should remain zero

	// detect EEPROM
	write32(REG_EEPROM, 1);
	eeprom_exists = false;
	for(int i = 0; i < 1000; ++i) {
		uint32_t val = read32(REG_EEPROM);
		if(val & 0x10) {
			eeprom_exists = true;
			break;
		}
	}

	// Read MAC address
	if(eeprom_exists) {
		uint16_t macword = eeprom_read(0);
		mac[0] = macword & 0xff;
		mac[1] = macword >> 8;
		macword = eeprom_read(1);
		mac[2] = macword & 0xff;
		mac[3] = macword >> 8;
		macword = eeprom_read(2);
		mac[4] = macword & 0xff;
		mac[5] = macword >> 8;
	} else {
		// TODO: do all i217 cards without EEPROM have bar_type memory?
		for(size_t i = 0; i < 6; ++i) {
			mac[i] = read8(MEM_MAC);
		}
	}

	// Enable bus mastering
	set_pci_config(0, 0x04, get_pci_config(0, 0x04) | (1 << 2));

	// Enable memory write
	set_pci_config(0, 0x04, get_pci_config(0, 0x04) | (1 << 4));

	// Initialize send & receive buffers
	auto const buffer_size = E1000_BUFFER_SIZE;
	auto const buffer_size_tag = RCTL_BSIZE_2048;
	
	write32(REG_RCTRL, 0);
	write32(REG_TCTRL, 0);

	buffers = get_map_virtual()->allocate_contiguous_phys(
		  E1000_NUM_RX_DESC * sizeof(e1000_rx_desc)
		+ E1000_NUM_TX_DESC * sizeof(e1000_tx_desc)
		+ E1000_NUM_RX_DESC * buffer_size
		+ E1000_NUM_TX_DESC * buffer_size);
	memset(buffers.ptr, 0, buffers.size);

	assert(sizeof(e1000_rx_desc) == 16);
	assert(sizeof(e1000_tx_desc) == 16);

	rx_descs = reinterpret_cast<e1000_rx_desc*>(buffers.ptr);
	tx_descs = reinterpret_cast<e1000_tx_desc*>(rx_descs + E1000_NUM_RX_DESC);
	auto *rx_buffers_start = reinterpret_cast<uint8_t*>(tx_descs + E1000_NUM_TX_DESC);
	auto *tx_buffers_start = rx_buffers_start + E1000_NUM_RX_DESC * buffer_size;

	assert(sizeof(rx_desc_bufs) / sizeof(rx_desc_bufs[0]) == E1000_NUM_RX_DESC);
	for(size_t i = 0; i < E1000_NUM_RX_DESC; ++i) {
		rx_desc_bufs[i] = reinterpret_cast<uint8_t*>(rx_buffers_start + i * buffer_size);
		rx_descs[i].addr = reinterpret_cast<uint64_t>(
			get_map_virtual()->to_physical_address(rx_desc_bufs[i]));
		rx_descs[i].status = 0;
	}

	assert(sizeof(tx_desc_bufs) / sizeof(tx_desc_bufs[0]) == E1000_NUM_TX_DESC);
	for(size_t i = 0; i < E1000_NUM_TX_DESC; ++i) {
		tx_desc_bufs[i] = reinterpret_cast<uint8_t*>(tx_buffers_start + i * buffer_size);
		tx_descs[i].addr = reinterpret_cast<uint64_t>(
			get_map_virtual()->to_physical_address(tx_desc_bufs[i]));
		tx_descs[i].cmd = 0;
		tx_descs[i].status = TSTA_DD;
	}

	asm volatile ("": : :"memory");
	write32(REG_RXDESCLO, reinterpret_cast<uint32_t>(
		get_map_virtual()->to_physical_address(rx_descs)));
	write32(REG_RXDESCHI, 0 /* upper 32 bits of address */);
	write32(REG_RXDESCLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc));
	write32(REG_RXDESCHEAD, 0);
	write32(REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);
	rx_current = 0;
	asm volatile ("": : :"memory");
	write32(REG_RCTRL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LBM_NONE
		| RTCL_RDMTS_HALF | RCTL_BAM | RCTL_SECRC | buffer_size_tag);

	write32(REG_TXDESCLO, reinterpret_cast<uint32_t>(
		get_map_virtual()->to_physical_address(tx_descs)));
	write32(REG_TXDESCHI, 0 /* upper 32 bits of address */);
	write32(REG_TXDESCLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc));
	write32(REG_TXDESCHEAD, 0);
	write32(REG_TXDESCTAIL, E1000_NUM_TX_DESC - 1);
	tx_current = 0;
	asm volatile ("": : :"memory");

	// TODO figure out all the correct flags
	write32(REG_TCTRL,  TCTL_EN
        | TCTL_PSP
        | (15 << TCTL_CT_SHIFT)
        | (64 << TCTL_COLD_SHIFT)
        | TCTL_RTLC);
	asm volatile ("": : :"memory");

	// Enable link
	write32(REG_CTRL, read32(REG_CTRL) | ECTRL_SLU);

	// Enable interrupts
	uint16_t interrupt_line = get_pci_config(0, 0x3c) & 0xff;
	register_irq(interrupt_line);

	// Enable interrupt line
	set_pci_config(0, 0x04, get_pci_config(0, 0x04) | (1 << 10));

	write32(REG_IMS, UINT32_MAX);
	write32(REG_IMC, UINT32_MAX);
	write32(REG_IMS, 1 << 7 /* RX Timer */);

	initialized = true;
	return 0;
}

cloudabi_errno_t intel_i217_device::get_mac_address(uint8_t m[6]) {
	assert(initialized);

	memcpy(m, mac, 6);
	return 0;
}

cloudabi_errno_t intel_i217_device::send_ethernet_frame(uint8_t *frame, size_t length) {
	auto &tx_desc = tx_descs[tx_current];

	uint8_t *buf = tx_desc_bufs[tx_current];
	assert(reinterpret_cast<void*>(tx_desc.addr) == get_map_virtual()->to_physical_address(buf));
	memcpy(buf, frame, length);
	assert(length <= E1000_BUFFER_SIZE);

	tx_desc.length = length;
	tx_desc.cmd = CMD_EOP | CMD_IFCS | CMD_RS | CMD_RPS;
	tx_desc.status = 0;

	tx_current = (tx_current + 1) % E1000_NUM_TX_DESC;
	asm volatile ("": : :"memory");
	write32(REG_TXDESCTAIL, tx_current);
	while(!(tx_desc.status & 0xff)) {
		asm volatile ("": : :"memory");
	}
	return 0;
}

void intel_i217_device::timer_event()
{
	handle_receive();
}

void intel_i217_device::handle_irq(uint8_t)
{
	handle_receive();
}

void intel_i217_device::handle_receive()
{
	while((rx_descs[rx_current].status & 1)) {
		uint8_t *buf = rx_desc_bufs[rx_current];
		auto length = rx_descs[rx_current].length;
		assert(length <= E1000_BUFFER_SIZE);
		ethernet_frame_received(buf, length);

		rx_descs[rx_current].status = 0;
		rx_current = (rx_current + 1) % E1000_NUM_RX_DESC;
	}
}
