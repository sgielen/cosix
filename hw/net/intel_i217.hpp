#pragma once

#include <hw/pci_bus.hpp>
#include <hw/net/ethernet_device.hpp>
#include <hw/interrupt.hpp>
#include <global.hpp>

namespace cloudos {

struct intel_i217_device : public ethernet_device, public pci_device, public irq_handler {
	intel_i217_device(pci_bus *parent, int bus_device);
	~intel_i217_device() override;

	const char *description() override;

	cloudabi_errno_t eth_init() override;

	cloudabi_errno_t get_mac_address(uint8_t mac[6]) override;
	cloudabi_errno_t send_ethernet_frame(uint8_t *frame, size_t length) override;

	void handle_irq(uint8_t) override;
	void timer_event() override;

private:
	uint16_t eeprom_read(uint8_t offset);
	void handle_receive();

	bool initialized = false;
	bool eeprom_exists = false;
	uint8_t mac[6];
	Blk buffers;
	struct e1000_rx_desc *rx_descs = nullptr;
	uint8_t rx_current = 0;
	struct e1000_tx_desc *tx_descs = nullptr;
	uint8_t tx_current = 0;

	uint8_t *rx_desc_bufs[32];
	uint8_t *tx_desc_bufs[8];
};

struct intel_i217_driver : public driver {
	const char *description() override;
	device *probe_pci_device(pci_bus *bus, int device) override;
};

}
