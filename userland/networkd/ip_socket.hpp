#pragma once
#include "networkd.hpp"
#include <string>

namespace networkd {

struct ip_socket {
	ip_socket(transport_proto proto, std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, int fd);
	virtual ~ip_socket();

	inline transport_proto get_transport_proto() { return proto; }

	inline bool matches_received_packet(std::string src_ip, std::string dest_ip) {
		return (get_local_ip() == std::string(4, 0) || get_local_ip() == dest_ip)
		    && (get_peer_ip().empty() || get_peer_ip() == src_ip);
	}

	// 0.0.0.0 if accepting packets to any IP
	inline std::string get_local_ip() { return local_ip; }
	inline uint16_t get_local_port() { return local_port; }

	// empty if accepting packets from anywhere
	inline std::string get_peer_ip() { return peer_ip; }
	inline uint16_t get_peer_port() { return peer_port; }

	virtual bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) = 0;
	virtual void start() = 0;

private:
	transport_proto proto;

	std::string local_ip;
	uint16_t local_port;

	std::string peer_ip;
	uint16_t peer_port;

	int fd;
};

}
