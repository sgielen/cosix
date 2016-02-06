#pragma once

#include "hw/device.hpp"
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

struct ethernet_interface;

struct ethernet_device : public device
{
	ethernet_device(device *parent);

	virtual error_t init() override final;

	virtual error_t eth_init() = 0;
	virtual error_t get_mac_address(char mac[6]) = 0;
	virtual error_t send_ethernet_frame(uint8_t *frame, size_t length) = 0;

protected:
	error_t ethernet_frame_received(uint8_t *frame, size_t length);
	ethernet_interface *get_interface();

private:
	/* this pointer is owned by the interface_store */
	ethernet_interface *interface;
};

}
