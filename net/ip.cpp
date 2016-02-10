#include "net/ip.hpp"
#include "net/udp.hpp"
#include "net/tcp.hpp"
#include "net/icmp.hpp"
#include "net/protocol_store.hpp"
#include "net/interface_store.hpp"
#include "net/interface.hpp"
#include "global.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/in.h"
#include "oslibc/string.h"
#include "memory/allocator.hpp"

using namespace cloudos;

error_t ip_implementation::received_ip_packet(interface *iface, uint8_t *packet, size_t length)
{
	uint8_t ip_version = packet[0] >> 4;
	if(ip_version == 4) {
		return received_ipv4_packet(iface, packet, length);
	} else if(ip_version == 6) {
		return received_ipv6_packet(iface, packet, length);
	} else {
		get_vga_stream() << "Packet of unknown IP version " << ip_version << " received on some interface\n";
		return error_t::invalid_argument;
	}
}

error_t ip_implementation::received_ipv4_packet(interface *iface, uint8_t *packet, size_t length)
{
	if(length < 20) {
		// shortest IP header does not fit in packet
		return error_t::invalid_argument;
	}

	uint16_t header_length = (packet[0] & 0x0f) * 4;
	if(header_length < 20 || length < header_length) {
		// IP header is too short or does not fit in packet
		return error_t::invalid_argument;
	}

	uint16_t total_length = ntoh(*reinterpret_cast<uint16_t*>(packet[2]));
	if(total_length < header_length) {
		// total length does not include header
		return error_t::invalid_argument;
	}
	if(length < total_length) {
		// actual IP packet does not fit in packet
		return error_t::invalid_argument;
	}

	get_vga_stream() << dec << "Total IP packet length: " << total_length << "\n";

	if(length != total_length) {
		get_vga_stream() << "Packet contains more than just header and payload, since it is "
		    << uint32_t(length) << " bytes, the IP header length is " << header_length << " and the total IP length is " << total_length << "\n";
	}

	const uint8_t IPV4_FLAG_MORE_FRAGMENTS = 2;

	uint8_t flags = packet[6] >> 5;
	uint16_t fragment_offset = ntoh(*reinterpret_cast<uint16_t*>(packet[6]) & 0x1fff);
	if(flags & IPV4_FLAG_MORE_FRAGMENTS || fragment_offset != 0) {
		// fragmented packets currently unsupported
		return error_t::invalid_argument;
	}

	// TODO: TTL checking
	// TODO: checksum checking

	uint8_t *ip_source = packet + 12;
	uint8_t *ip_dest = packet + 16;

	get_vga_stream() << "Received IPv4 packet from " << ip_source[0] << "." << ip_source[1] << "." << ip_source[2] << "." << ip_source[3]
	    << " to " << ip_dest[0] << "." << ip_dest[1] << "." << ip_dest[2] << "." << ip_dest[3] << "\n";

	// TODO: this should be some indirection with multiple protocol
	// implementations
	uint8_t protocol = packet[9];
	const uint8_t ICMP_PROTOCOL = 1;
	const uint8_t TCP_PROTOCOL = 6;
	const uint8_t UDP_PROTOCOL = 17;
	if(protocol == ICMP_PROTOCOL) {
		return get_protocol_store()->icmp->received_ipv4(iface, packet + header_length,
			length - header_length, ip_source, ip_dest);
	} else if(protocol == TCP_PROTOCOL) {
		return get_protocol_store()->tcp->received_ipv4(iface, packet + header_length,
			length - header_length, ip_source, ip_dest);
	} else if(protocol == UDP_PROTOCOL) {
		return get_protocol_store()->udp->received_ipv4(iface, packet + header_length,
			length - header_length, ip_source, ip_dest);
	} else {
		get_vga_stream() << "  It has an unknown protocol byte: " << protocol << "\n";
		return error_t::invalid_argument;
	}
}

error_t ip_implementation::received_ipv6_packet(interface*, uint8_t*, size_t)
{
	get_vga_stream() << "Received IPv6 packet on some interface, ignoring for now\n";
	return error_t::invalid_argument;
}

error_t ip_implementation::send_ipv4_packet(uint8_t *payload, size_t length, ipv4addr_t source, ipv4addr_t destination, protocol_t inner_protocol)
{
	// TODO: we currently assume we need to send every packet to eth0;
	// instead, we should check our routing table to find the right
	// destination

	// TODO: iovecs would be super-useful here
	uint16_t header_length = 20;
	uint16_t ip_length = length + header_length;
	uint8_t *packet = reinterpret_cast<uint8_t*>(get_allocator()->allocate(ip_length));
	packet[0] = 0x40 | (header_length / 4); // version + header words
	packet[1] = 0; // DSCP + ECN
	packet[2] = ip_length >> 8; // total length
	packet[3] = ip_length & 0xff; // total length
	packet[4] = 0xf5; // identification TODO 0
	packet[5] = 0xe9; // identification
	packet[6] = 0; // fragment flags & offset
	packet[7] = 0; // fragment offset
	packet[8] = 0xff; // TTL
	// protocol, TODO this should be nicer
	packet[9] = inner_protocol == protocol_t::icmp ? 1 : inner_protocol == protocol_t::tcp ? 6 : inner_protocol == protocol_t::udp ? 17 : 0;
	packet[10] = 0; // checksum
	packet[11] = 0; // checksum
	memcpy(packet + 12, source, 4);
	memcpy(packet + 16, destination, 4);
	memcpy(packet + 20, payload, length);

	// compute checksum
	uint16_t *p16 = reinterpret_cast<uint16_t*>(packet);
	uint32_t sum = 0;
	for(size_t i = 0; i < header_length / 2; ++i) {
		sum += p16[i];
	}
	uint16_t checksum = ~((sum >> 16) + sum & 0xffff);
	packet[10] = checksum & 0xff;
	packet[11] = checksum >> 8;

	auto *list = get_interface_store()->get_interfaces();
	for(; list; list = list->next) {
		if(strcmp(list->data->get_name(), "eth0") == 0) {
			return list->data->send_packet(packet, ip_length);
		}
	}

	get_vga_stream() << "Found no interface to send packet to, dropping\n";
	return error_t::no_error;
}
