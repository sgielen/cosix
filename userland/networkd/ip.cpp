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

	auto protocol = static_cast<transport_proto>(hdr->proto);

	if(protocol == transport_proto::icmp) {
		// TODO: handle ICMP myself
		return;
	} else if(protocol != transport_proto::udp && protocol != transport_proto::tcp) {
		// don't know about this protocol, drop it
		return;
	}

	std::string ip_src(reinterpret_cast<const char*>(&hdr->source_ip), 4);
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

	std::vector<std::shared_ptr<ip_socket>> recv_sockets;

	do {
		std::lock_guard<std::mutex> lock(sockets_mtx);
		auto proto_it = sockets.find(protocol);
		if(proto_it == sockets.end()) {
			// not listening on this proto, drop it
			break;
		}

		auto &ip_to_sock = proto_it->second;
		auto ip_it = ip_to_sock.find(ip_dst);
		if(ip_it == ip_to_sock.end()) {
			// not listening on this IP directly, maybe on the wildcard?
		} else {
			for(auto &sock : ip_it->second) {
				auto shared_sock = sock.lock();
				if(shared_sock) {
					recv_sockets.push_back(shared_sock);
				}
			}
		}

		std::string wildcard(4, 0);
		ip_it = ip_to_sock.find(wildcard);
		if(ip_it == ip_to_sock.end()) {
			// not listening on wildcard either
		} else {
			for(auto &sock : ip_it->second) {
				auto shared_sock = sock.lock();
				if(shared_sock) {
					recv_sockets.push_back(shared_sock);
				}
			}
		}
	} while(0); // drop the lock around sockets

	bool packet_sent = false;
	for(auto &sock : recv_sockets) {
		if(sock->matches_received_packet(ip_src, ip_dst)) {
			if(sock->handle_packet(iface, frame, framelen, ip_offset, payload_offset, payload_length)) {
				packet_sent = true;
				// but allow other sockets to handle it as well
			}
		}
	}
	if(!packet_sent) {
		// TODO: actively refuse packet, nothing is handling it
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

cloudabi_errno_t ip::register_socket(std::shared_ptr<ip_socket> socket)
{
	std::lock_guard<std::mutex> lock(sockets_mtx);

	auto proto = socket->get_transport_proto();
	assert(proto == transport_proto::udp || proto == transport_proto::tcp);
	auto local_ip = socket->get_local_ip();

	// TODO: check if any iface even _has_ this local IP?
	// TODO: check if this socket doesn't conflict with any other already-listening
	// sockets, e.g. two exclusive UDP sockets?

	sockets[proto][local_ip].push_back(socket);
	return 0;
}
