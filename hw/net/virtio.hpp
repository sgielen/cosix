#pragma once

#include "hw/pci_bus.hpp"
#include "hw/net/ethernet_device.hpp"
#include "global.hpp"

namespace cloudos {

struct virtq;

struct virtio_net_device : public ethernet_device {
	virtio_net_device(pci_bus *parent, int bus_device);

	const char *description() override;

	error_t eth_init() override;

	error_t get_mac_address(char mac[6]) override;
	error_t send_ethernet_frame(uint8_t *frame, size_t length) override;

	void timer_event() override;

private:
	error_t check_new_packets();

	uint8_t mac[6];
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
