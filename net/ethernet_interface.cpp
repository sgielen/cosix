#include "net/ethernet_interface.hpp"
#include "net/arp.hpp"
#include "hw/net/ethernet_device.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "oslibc/string.h"
#include "oslibc/in.h"

using namespace cloudos;

ethernet_interface::ethernet_interface(ethernet_device *d)
: interface(hwtype_t::ethernet)
, device(d)
{}

cloudabi_errno_t ethernet_interface::send_packet(uint8_t *packet, size_t length) {
	char mac[6];
	auto res = device->get_mac_address(mac);
	if(res != 0) {
		return res;
	}

	size_t frame_size = length + 18 /* length of ethernet header + crc */;
	uint8_t *frame = reinterpret_cast<uint8_t*>(get_allocator()->allocate(frame_size));
	// TODO: look up destination MAC
	memcpy(frame, "\xff\xff\xff\xff\xff\xff", 6);
	memcpy(frame + 6, mac, 6);
	frame[12] = 0x08; // IPv4
	frame[13] = 0x00; // TODO: have upper layer communicate what kind of (IP?) packet this is
	memcpy(frame + 14, packet, length);

	// TODO: ethernet CRC
	frame[length + 14] = 0;
	frame[length + 15] = 0;
	frame[length + 16] = 0;
	frame[length + 17] = 0;

	return device->send_ethernet_frame(frame, frame_size);
}

cloudabi_errno_t ethernet_interface::ethernet_frame_received(uint8_t *frame, size_t length)
{
	size_t frame_length = 14;
	uint16_t ethertype = ntoh(*reinterpret_cast<uint16_t*>(frame + 12));

	// https://en.wikipedia.org/wiki/EtherType
	if(ethertype == 0x8100) {
		frame_length += 4;
		ethertype = ntoh(*reinterpret_cast<uint16_t*>(frame + 16));
	}

	if(ethertype <= 1500) {
		// actually, this is length instead of ethertype; we should support this but
		// can't detect ethertype ourselves yet, so warn and fail
		get_vga_stream() << "Refusing to parse ethernet frame without EtherType (length " << ethertype << ")\n";
		return EINVAL;
	}

	if(ethertype == 0x0800 || ethertype == 0x86dd) {
		// IPv4 or IPv6
		return received_ip_packet(frame, length, protocol_t::ethernet, 14);
	}

	if(ethertype == 0x0806) {
		// ARP
		return get_protocol_store()->arp->received_arp(this, frame, length);
	}

	get_vga_stream() << "Ignoring ethernet frame with unknown ethertype " << ethertype << "\n";
	return EINVAL;
}
