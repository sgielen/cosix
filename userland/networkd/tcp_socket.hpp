#pragma once
#include "ip_socket.hpp"
#include <queue>
#include <thread>
#include <map>

namespace networkd {

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

struct tcp_incoming_connection {
	std::string frame;
	size_t ip_offset;
	size_t tcp_offset;
	size_t payload_offset;
	size_t payload_length;
};

struct tcp_socket : public ip_socket {
	tcp_socket(std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, cosix::pseudofd_t pseudofd, int reversefd);
	~tcp_socket() override;

	enum sockstatus_t {
		LISTENING, CONNECTING, CONNECTED, SHUTDOWN
	};

	virtual cloudabi_errno_t establish() override;
	bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) override;

private:
	void pwrite(cosix::pseudofd_t, off_t, const char*, size_t) override;
	size_t pread(cosix::pseudofd_t, off_t, char*, size_t) override;
	cosix::pseudofd_t sock_accept(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	void sock_stat_get(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	std::shared_ptr<tcp_socket> get_child(cosix::pseudofd_t ps);

	void send_tcp_frame(bool syn, bool ack);

	std::mutex wc_mtx;
	std::condition_variable incoming_cv;

	// all guarded by wc_mtx:
	sockstatus_t status;
	std::queue<tcp_incoming_connection> waiting_connections;
	cosix::pseudofd_t last_subsocket = 0;
	uint32_t send_ack_num = 0; // the next sequence nr to ACK
	std::string recv_buffer;
	uint32_t send_seq_num = 0; // the next sequence nr to send
	std::string send_buffer;

	std::mutex child_sockets_mtx;
	std::map<cosix::pseudofd_t, std::shared_ptr<tcp_socket>> child_sockets;
};

}
