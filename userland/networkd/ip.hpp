#pragma once
#include <sys/uio.h>
#include <memory>
#include <map>
#include <vector>
#include <thread>
#include <list>
#include "util.hpp"
#include "networkd.hpp"
#include "tcp.hpp"
#include "udp.hpp"

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

void compute_ip_checksum(ip_header &ip_hdr);

struct ip {
	ip();

	// Handle an incoming IP packet
	void handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset);

	// Send an IP packet
	cloudabi_errno_t send_packet(std::vector<iovec> const&, std::string ip);

	struct tcp &get_tcp_impl() { return tcp_impl; }
	struct udp &get_udp_impl() { return udp_impl; }

private:
	struct tcp tcp_impl;
	struct udp udp_impl;
};

}
