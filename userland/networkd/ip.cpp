#include "ip.hpp"
#include "ip_socket.hpp"
#include "networkd.hpp"
#include "routing_table.hpp"
#include <cassert>

using namespace networkd;

ip::ip() {}

void ip::handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset)
{
	if(ip_offset + sizeof(ip_header) > framelen) {
		// header won't fit
		return;
	}

	auto *hdr = reinterpret_cast<const ip_header*>(frame + ip_offset);
	if(hdr->version != 4) {
		// don't understand IP version
		return;
	}

	if(hdr->ihl < 5) {
		// header too short
		return;
	}

	// TODO: check header checksum

	size_t header_length = hdr->ihl * 4;
	size_t payload_offset = ip_offset + header_length;
	size_t total_length = htons(hdr->total_len);
	size_t payload_length = total_length - header_length;

	if(total_length + ip_offset > framelen) {
		// packet too short
		return;
	}

	uint16_t frag_offset = htons(hdr->frag_offset_1 << 8 | hdr->frag_offset_2);
	if((hdr->flags & 1 /* more fragments */) || frag_offset != 0) {
		// TODO: handle IP fragmentation
		return;
	}

	if(hdr->ttl == 1) {
		// TODO: send an ICMP Time Exceeded packet and don't handle
	}

	std::string ip_dst(reinterpret_cast<const char*>(&hdr->dest_ip), 4);

	if(ip_dst != std::string(4, 0xff)) {
		// check if this interface even has this destination IP
		bool have_ip = false;
		for(auto &ip : iface->get_ipv4addrs()) {
			if(ip_dst == std::string(ip.ip, 4)) {
				have_ip = true;
				break;
			}
		}

		if(!have_ip) {
			// don't have this IP, drop it
			return;
		}
	}

	auto protocol = static_cast<transport_proto>(hdr->proto);

	if(protocol == transport_proto::tcp) {
		tcp_impl.handle_packet(iface, frame, framelen, ip_offset, payload_offset, payload_length);
	} else if(protocol == transport_proto::udp) {
		udp_impl.handle_packet(iface, frame, framelen, ip_offset, payload_offset, payload_length);
	} else if(protocol == transport_proto::icmp) {
		// TODO: handle ICMP myself
	} else {
		// don't know about this protocol, drop it
	}
}

cloudabi_errno_t ip::send_packet(std::vector<iovec> const &iov, std::string ip)
{
	auto route = get_routing_table().routing_rule_for_ip(ip);
	if(!route) {
		// don't know how to route to this IP
		return CLOUDABI_ENETUNREACH;
	}

	auto gateway = route->first;
	auto interface = route->second;
	if(!gateway.empty()) {
		// route to the gateway instead
		ip = gateway;
	}

	return interface->send_ip_packet(iov, ip);
}

void networkd::compute_ip_checksum(ip_header &ip_hdr) {
	uint16_t *ip_hdr_16 = reinterpret_cast<uint16_t*>(&ip_hdr);
	uint32_t short_sum = 0;
	for(size_t i = 0; i < sizeof(ip_hdr) / 2; ++i) {
		short_sum += ip_hdr_16[i];
	}
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	ip_hdr.checksum = ~short_sum;
}
