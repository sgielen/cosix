#pragma once
#include <sys/uio.h>
#include <memory>
#include <map>
#include <vector>
#include <thread>
#include <list>
#include "util.hpp"
#include "networkd.hpp"
#include "udp_socket.hpp"

namespace networkd {

struct udp_header {
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
};

struct udp {
	udp();

	// Register a socket for incoming packets
	cloudabi_errno_t register_socket(std::shared_ptr<udp_socket> socket);

	// Handle an incoming UDP packet
	void handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t udp_offset, size_t udp_length);

private:
	std::mutex sockets_mtx;
	// a map of UDP local port to UDP sockets
	std::map<uint16_t, std::shared_ptr<udp_socket>> sockets;
};

}
