#pragma once
#include <sys/uio.h>
#include <memory>
#include <map>
#include <vector>
#include <thread>
#include <list>
#include "util.hpp"
#include "networkd.hpp"

namespace networkd {

struct ip_socket;
struct interface;

struct ip_header {
// This is little-endian byte order
	uint8_t ihl : 4;
	uint8_t version : 4;
	uint8_t tos;
	uint16_t total_len;
	uint16_t ident;
	uint8_t frag_offset_1 : 5;
	uint8_t flags : 3;
	uint8_t frag_offset_2;
	uint8_t ttl;
	uint8_t proto;
	uint16_t checksum;
	uint32_t source_ip;
	uint32_t dest_ip;
};

struct ip {
	ip();

	// Handle an incoming IP packet
	void handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset);

	// Send an IP packet
	cloudabi_errno_t send_packet(std::vector<iovec> const&, std::string ip);

	// Register a socket for incoming packets
	cloudabi_errno_t register_socket(std::shared_ptr<ip_socket> socket);

	// Create a new socket. IP is in packed format. transport_proto must be
	// tcp or udp. The socket state will be 'INITIALIZING', so you will
	// need to call connect() or listen() on the socket
	std::shared_ptr<ip_socket> create_socket(transport_proto p, uint16_t port, std::string ip);

	// TODO: build a job that regularly cleans out the sockets list, removing
	// weak pointers as they become expired, maps as they become empty and IPs
	// as our interfaces lose them

private:
	std::mutex sockets_mtx;
	std::map<transport_proto,
		std::map<std::string /* listening on IP */,
			std::list<std::weak_ptr<ip_socket>> /* sockets */
		>
	> sockets;
};

}
