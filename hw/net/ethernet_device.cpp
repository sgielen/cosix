#include "hw/net/ethernet_device.hpp"
#include "net/ethernet_interface.hpp"
#include "net/interface_store.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "oslibc/string.h"

using namespace cloudos;

ethernet_device::ethernet_device(device *p)
: device(p)
, interface(nullptr)
{
	interface = get_allocator()->allocate<ethernet_interface>();
	new(interface) ethernet_interface(this);
}

cloudabi_errno_t ethernet_device::init()
{
	cloudabi_errno_t res = get_interface_store()->register_interface(interface, "eth");
	if(res != 0) {
		return res;
	}

	return eth_init();
}

cloudabi_errno_t ethernet_device::ethernet_frame_received(uint8_t *frame, size_t length)
{
	char my_mac[6];
	auto res = get_mac_address(my_mac);
	if(res != 0) {
		return res;
	}

	// [6-byte destination MAC][6-byte source MAC][optional 4-byte vlan tag]
	// [2-byte ethertype/length][payload][4-byte CRC]

	// frame starts with 6-byte MAC destination
	bool broadcast_frame = memcmp(frame, "\xff\xff\xff\xff\xff\xff", 6) == 0;
	bool frame_to_me = memcmp(frame, my_mac, 6) == 0;

	if(broadcast_frame || frame_to_me) {
		return interface->ethernet_frame_received(frame, length);
	}

	// drop the packet, not intended for me
	return 0;
}

ethernet_interface *ethernet_device::get_interface()
{
	return interface;
}
