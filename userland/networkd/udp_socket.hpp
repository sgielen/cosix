#pragma once
#include "ip_socket.hpp"
#include <queue>
#include <thread>
#include <map>

namespace networkd {

struct udp_header {
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
};

struct udp_message {
	std::string frame;
	size_t ip_offset;
	size_t udp_offset;
	size_t payload_offset;
	size_t payload_length;
};

struct udp_socket : public ip_socket {
	udp_socket(std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, cosix::pseudofd_t pseudofd, int reversefd);
	~udp_socket() override;

	bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) override;

private:
	void pwrite(cosix::pseudofd_t, off_t, const char*, size_t) override;
	size_t pread(cosix::pseudofd_t, off_t, char*, size_t) override;
	cosix::pseudofd_t sock_accept(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	void sock_stat_get(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	std::shared_ptr<udp_socket> get_child(cosix::pseudofd_t ps);

	std::mutex wm_mtx;
	std::condition_variable wm_cv;

	// both guarded by wm_mtx:
	std::queue<udp_message> waiting_messages;
	cosix::pseudofd_t last_subsocket = 0;

	std::mutex child_sockets_mtx;
	std::map<cosix::pseudofd_t, std::shared_ptr<udp_socket>> child_sockets;
};

}
