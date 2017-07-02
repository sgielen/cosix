#include "net/ethernet_interface.hpp"
#include "hw/net/ethernet_device.hpp"
#include "global.hpp"
#include "oslibc/string.h"
#include "oslibc/in.h"

using namespace cloudos;

ethernet_interface::ethernet_interface(ethernet_device *d)
: interface(hwtype_t::ethernet)
, device(d)
{}

cloudabi_errno_t ethernet_interface::send_frame(uint8_t *frame, size_t frame_size) {
	return device->send_ethernet_frame(frame, frame_size);
}

cloudabi_errno_t ethernet_interface::ethernet_frame_received(uint8_t *frame, size_t length)
{
	return received_frame(frame, length);
}
