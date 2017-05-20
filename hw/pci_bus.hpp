#pragma once

#include "hw/driver.hpp"
#include "hw/device.hpp"
#include <stdint.h>
#include <memory/allocation.hpp>

namespace cloudos {

struct pci_driver : public driver {
	const char *description() override;
	device *probe_root_device(device *root) override;
};

/**
 * This device is a device beneath a PCI bus.
 */
struct pci_device : virtual device {
	pci_device(pci_bus *bus, uint8_t dev);
	~pci_device() override;

	inline pci_bus *get_pci_bus() { return bus; }
	inline uint8_t get_pci_dev() { return dev; }

	uint16_t get_device_id();
	uint16_t get_vendor_id();
	uint16_t get_subsystem_id();
	uint32_t get_pci_config(uint8_t function, uint8_t reg);
	void set_pci_config(uint8_t function, uint8_t reg, uint32_t value);

	void write8(uint16_t offset, uint8_t value);
	void write16(uint16_t offset, uint16_t value);
	void write32(uint16_t offset, uint32_t value);
	uint8_t read8(uint16_t offset);
	uint16_t read16(uint16_t offset);
	uint32_t read32(uint16_t offset);

private:
	void init_pci_device();

	pci_bus *bus = nullptr;
	uint8_t dev = 0;

	// 1 = IO, 2 = Mem
	uint8_t bar_type = 0;
	uint64_t base_address = 0;
	Blk mapping;
};

/**
 * This device represents an unclaimed PCI device.
 */
struct pci_unused_device : public pci_device {
	pci_unused_device(pci_bus *bus, uint8_t dev);
	~pci_unused_device() override;

	const char *description() override;
	cloudabi_errno_t init() override;

private:
	Blk descr;
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
	uint32_t get_pci_config(uint8_t device, uint8_t function, uint8_t reg);
	void set_pci_config(uint8_t device, uint8_t function, uint8_t reg, uint32_t value);

private:
	static uint32_t read_pci_config(uint8_t bus, uint8_t device,
		uint8_t function, uint8_t reg);
	static void write_pci_config(uint8_t bus, uint8_t device,
		uint8_t function, uint8_t reg, uint32_t value);
};

}
