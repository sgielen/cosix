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

struct tcp_outgoing_segment {
	std::string segment; // including IP + TCP headers
	uint32_t seqnum;
	uint16_t segsize;
	cloudabi_timestamp_t ack_deadline;
};

struct tcp_socket : public ip_socket {
	tcp_socket(std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, cosix::pseudofd_t pseudofd, int reversefd);
	~tcp_socket() override;

	enum sockstatus_t {
		LISTENING, CONNECTING, CONNECTED, SHUTDOWN, OURS_CLOSED, THEIRS_CLOSED, CLOSED
	};

	virtual cloudabi_errno_t establish() override;
	bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) override;

	virtual void timed_out() override;
	virtual cloudabi_timestamp_t next_timeout() override;

private:
	void pwrite(cosix::pseudofd_t, off_t, const char*, size_t) override;
	size_t pread(cosix::pseudofd_t, off_t, char*, size_t) override;
	cosix::pseudofd_t sock_accept(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	void sock_stat_get(cosix::pseudofd_t pseudo, cloudabi_sockstat_t *ss) override;
	std::shared_ptr<tcp_socket> get_child(cosix::pseudofd_t ps);

	// only call these functions if you already have the wc_mtx:
	void send_tcp_frame(bool syn, bool ack, std::string data = std::string(), bool fin = false, bool rst = false);
	void pump_segment_queue();

	std::mutex wc_mtx;
	std::condition_variable incoming_cv;

	// all guarded by wc_mtx:
	sockstatus_t status;
	std::queue<tcp_incoming_connection> waiting_connections;
	cosix::pseudofd_t last_subsocket = 0;
	uint32_t send_ack_num = 0; // the next sequence nr to ACK
	std::string recv_buffer;
	uint32_t send_seq_num = 0; // the next sequence nr to send
	std::deque<tcp_outgoing_segment> outgoing_segments; // in order of sequence number
	size_t send_window_size = 0;
	cloudabi_timestamp_t next_ack_deadline = 0;

	std::mutex child_sockets_mtx;
	std::map<cosix::pseudofd_t, std::shared_ptr<tcp_socket>> child_sockets;
};

}
