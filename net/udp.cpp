#include "net/udp.hpp"
#include "net/dhcp.hpp"
#include "net/elfrun.hpp"
#include "oslibc/in.h"
#include "oslibc/string.h"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "hw/vga_stream.hpp"

using namespace cloudos;

error_t udp_implementation::received_ipv4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t ip_source, ipv4addr_t ip_destination)
{
	uint16_t *udp_header = reinterpret_cast<uint16_t*>(payload);
	uint16_t source_port = ntoh(udp_header[0]);
	uint16_t destination_port = ntoh(udp_header[1]);
	uint16_t udp_length = ntoh(udp_header[2]);
	// TODO: check checksum
	if(length < udp_length) {
		get_vga_stream() << "  It's a UDP message that's too large. Dropping.\n";
		return error_t::invalid_argument;
	}
	if(length != udp_length) {
		get_vga_stream() << "  It's a UDP message whose size doesn't make sense. Payload length is "
		    << uint32_t(length) << ", while UDP length is " << udp_length << "...\n";
		// this means udp packet is too small, but we can try to continue
	}
	get_vga_stream() << "  It's a UDP message from source port " << source_port << " to destination port " << destination_port << "\n";
	if(destination_port == 68) {
		return get_protocol_store()->dhcp->received_udp4(iface, payload + 8,
			udp_length - 8, ip_source, source_port, ip_destination, destination_port);
	} else if(destination_port == 4445) {
		return get_protocol_store()->elfrun->received_udp4(iface, payload + 8,
			udp_length - 8, ip_source, source_port, ip_destination, destination_port);
	}
	return error_t::no_error;
}

error_t udp_implementation::send_ipv4_udp(const uint8_t *payload, size_t length, ipv4addr_t source, uint16_t source_port, ipv4addr_t destination, uint16_t destination_port)
{
	// TODO: iovecs would be super-useful here
	uint16_t pseudo_ip_length = 12;
	uint16_t udp_length = length + 8;
	uint8_t *pseudo_packet = reinterpret_cast<uint8_t*>(get_allocator()->allocate(pseudo_ip_length + udp_length));
	uint16_t *udp_header = reinterpret_cast<uint16_t*>(pseudo_packet + pseudo_ip_length);
	udp_header[0] = hton(source_port);
	udp_header[1] = hton(destination_port);
	udp_header[2] = hton(udp_length);
	udp_header[3] = 0;
	memcpy(pseudo_packet + pseudo_ip_length + 8, payload, length);

	// TODO: compute checksum, although UDP allows leaving it at zero

	return get_protocol_store()->ip->send_ipv4_packet(pseudo_packet + pseudo_ip_length, udp_length, source, destination, protocol_t::udp);
}
