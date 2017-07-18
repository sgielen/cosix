#pragma once

#include "hw/pci_bus.hpp"
#include "hw/net/ethernet_device.hpp"
#include "global.hpp"

namespace cloudos {

struct address_mapping {
	void *physical;
	void *logical;
};

// TODO: there must be a better way to remember logical addresses for physical ones handed out to virtio
typedef linked_list<address_mapping*> address_mapping_list;

struct virtq;

struct virtio_net_device : public ethernet_device, public pci_device {
	virtio_net_device(pci_bus *parent, int bus_device);

	const char *description() override;

	cloudabi_errno_t eth_init() override;

	cloudabi_errno_t get_mac_address(uint8_t mac[6]) override;
	cloudabi_errno_t send_ethernet_frame(uint8_t *frame, size_t length) override;

	void timer_event() override;

private:
	cloudabi_errno_t add_buffer_to_avail(virtq *virtq, int buffer);
	cloudabi_errno_t check_new_packets();

	uint8_t mac[6];
	int last_readq_used_idx = 0;
	int last_writeq_idx = 0;
	uint64_t drv_features = 0;
	virtq *readq = nullptr;
	virtq *writeq = nullptr;
	address_mapping_list *mappings = nullptr;
};

struct virtio_net_driver : public driver {
	const char *description() override;
	device *probe_pci_device(pci_bus *bus, int device) override;
};

}
