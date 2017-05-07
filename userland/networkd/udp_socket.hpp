#pragma once
#include "ip_socket.hpp"
#include <queue>
#include <thread>

namespace networkd {

struct udp_header {
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
};

struct udp_socket : public ip_socket {
	udp_socket(std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, int fd);
	~udp_socket() override;

	bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) override;

private:
	void pwrite(cosix::pseudofd_t, off_t, const char*, size_t) override;
	size_t pread(cosix::pseudofd_t, off_t, char*, size_t) override;

	std::mutex wm_mtx;
	std::condition_variable wm_cv;
	std::queue<std::string> waiting_messages;
};

}
