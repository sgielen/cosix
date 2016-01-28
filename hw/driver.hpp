#pragma once

namespace cloudos {

struct device;
struct pci_bus;

/**
 * A driver can be seen as a factory for devices: it usually has support for a
 * specific model of a device made by a specific vendor, and it will create
 * device-derived objects when such a device is found somewhere.
 *
 * By default, a driver cannot do anything; bus-like devices will use abstract
 * driver subclasses like the pci_driver to help probe for new devices.
 */
struct driver {
	virtual ~driver() {}

	/**
	 * This method returns a single line of text that describes this
	 * driver. This value must be either static memory, or allocated and
	 * owned by the driver, and must never be freed by the caller. The
	 * value may become invalid or dangling after subsequent calls to
	 * methods of this driver.
	 */
	virtual const char *description() = 0;

	/**
	 * This method is called during the probing of a PCI bus, and should
	 * return either a new instance of a device on this PCI bus or zero if
	 * this driver cannot handle the device.
	 */
	virtual device *probe_pci_device(pci_bus *bus, int device) {
		(void)bus;
		(void)device;
		return nullptr;
	}

	/**
	 * This method is called exactly once for every registered driver. It
	 * creates a new instance of a device that is added as a child of the
	 * given root_device, or returns null.
	 */
	virtual device *probe_root_device(device *root) {
		(void)root;
		return nullptr;
	}
};

}
