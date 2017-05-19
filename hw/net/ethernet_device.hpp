#pragma once

#include "hw/device.hpp"
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

struct ethernet_interface;

struct ethernet_device : virtual device
{
	ethernet_device();
	virtual ~ethernet_device() override;

	virtual cloudabi_errno_t init() override final;

	virtual cloudabi_errno_t eth_init() = 0;
	virtual cloudabi_errno_t get_mac_address(char mac[6]) = 0;
	// Give an ethernet frame, but excluding its checksum
	virtual cloudabi_errno_t send_ethernet_frame(uint8_t *frame, size_t length) = 0;

protected:
	cloudabi_errno_t ethernet_frame_received(uint8_t *frame, size_t length);
	ethernet_interface *get_interface();

private:
	/* this pointer is owned by the interface_store */
	ethernet_interface *interface;
};

}
