#pragma once

#include "hw/driver.hpp"
#include "hw/device.hpp"
#include <stdint.h>

namespace cloudos {

struct pci_driver : public driver {
	const char *description() override;
	device *probe_root_device(device *root) override;
};

/**
 * This device represents the root PCI bus (bus 0) of the PC.
 */
struct pci_bus : public device {
	pci_bus(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	uint16_t get_device_id(uint8_t device);
	uint16_t get_vendor_id(uint8_t device);
	uint16_t get_subsystem_id(uint8_t device);
	uint32_t get_bar0(uint8_t device);

private:
	static uint32_t read_pci_config(uint8_t bus, uint8_t device,
		uint8_t function, uint8_t reg);
};

}
