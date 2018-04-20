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

struct ip_header;
struct tcp_socket;

struct tcp_header {
	uint16_t source_port;
	uint16_t dest_port;
	uint32_t seqnum; // sequence number of first byte of payload
	uint32_t acknum; // acknowledgement of last byte of sent payload
	/* little endian */
	uint8_t flag_ns : 1;
	uint8_t reserved : 3;
	uint8_t data_off : 4;

	uint8_t flag_fin : 1;
	uint8_t flag_syn : 1;
	uint8_t flag_rst : 1;
	uint8_t flag_psh : 1;
	uint8_t flag_ack : 1;
	uint8_t flag_urg : 1;
	uint8_t flag_ece : 1;
	uint8_t flag_cwr : 1;

	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_ptr;
};

void compute_tcp_checksum(tcp_header &tcp_hdr, std::string local_ip, std::string peer_ip, std::string const &data, size_t segment_size);

struct tcp_connection {
	std::string peer_ip;
	std::string local_ip;
	uint16_t peer_port;
	uint16_t local_port;
	bool operator<(tcp_connection const&) const;
};

struct tcp {
	tcp();

	// Register a socket for incoming packets
	cloudabi_errno_t register_socket(std::shared_ptr<tcp_socket> socket);

	// Unregister a socket
	void unregister_socket(std::shared_ptr<tcp_socket> socket);

	// Handle an incoming TCP packet
	void handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t tcp_offset, size_t tcp_length);

private:
	static void send_rst(tcp_connection const &conn, uint32_t acknum);

	cloudabi_errno_t locked_register_socket(std::shared_ptr<tcp_socket> socket);

	std::mutex sockets_mtx;
	// a map of all connected or establishing TCP (source,dest) pairs
	std::map<tcp_connection, std::shared_ptr<tcp_socket>> sockets;
};

}
