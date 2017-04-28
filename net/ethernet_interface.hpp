#pragma once

#include "net/interface.hpp"
#include "hw/net/ethernet_device.hpp"

namespace cloudos {

/**
 * An ethernet_interface is an implementation of an interface that is created
 * when an ethernet_device is constructed.
 */
struct ethernet_interface : public interface
{
	ethernet_interface(ethernet_device *device);
	virtual ~ethernet_interface() override {}

	cloudabi_errno_t get_mac_address(char mac[6]) {
		return device->get_mac_address(mac);
	}

	virtual cloudabi_errno_t send_frame(uint8_t *packet, size_t length) override;

private:
	friend struct ethernet_device;
	cloudabi_errno_t ethernet_frame_received(uint8_t *frame, size_t frame_length);

	/* This pointer is owned by its parent device */
	ethernet_device *device;
};

}
