#pragma once

#include "hw/pci_bus.hpp"
#include "hw/driver.hpp"

namespace cloudos {

struct virtq;

struct virtio_net_device : public device {
	virtio_net_device(pci_bus *parent, int bus_device);

	const char *description() override;

	error_t init() override;

private:
	pci_bus *bus;
	int bus_device;
	int last_readq_idx;
	int last_writeq_idx;
	virtq *readq;
	virtq *writeq;
};

struct virtio_net_driver : public driver {
	const char *description() override;
	device *probe_pci_device(pci_bus *bus, int device) override;
};

}
