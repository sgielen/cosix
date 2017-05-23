#include "tcp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>
#include "ip.hpp"
#include <cassert>

using namespace cosix;
using namespace networkd;

struct ipv4_pseudo_hdr {
	uint32_t source_ip;
	uint32_t dest_ip;
	uint8_t reserved;
	uint8_t protocol;
	uint16_t length;
};

tcp_socket::tcp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, pseudofd_t ps, int r)
: ip_socket(transport_proto::tcp, l_ip, l_p, p_ip, p_p, ps, r)
, status(p_ip.empty() ? sockstatus_t::LISTENING : sockstatus_t::CONNECTING)
{
	assert(status == sockstatus_t::CONNECTING || ps == 0);
}

tcp_socket::~tcp_socket()
{
}

bool tcp_socket::handle_packet(std::shared_ptr<interface>, const char *frame, size_t framelen, size_t ip_offset, size_t tcp_offset, size_t tcp_length)
{
	if(tcp_length < sizeof(tcp_header)) {
		// TCP header doesn't fit
		return false;
	}
	auto *hdr = reinterpret_cast<tcp_header const*>(frame + tcp_offset);

	if(htons(hdr->dest_port) != get_local_port()) {
		return false;
	}

	uint16_t header_size = hdr->data_off * 4;
	if(header_size < sizeof(tcp_header)) {
		// TCP header including options don't fit
		return false;
	}
	size_t payload_offset = tcp_offset + header_size;
	size_t payload_length = tcp_length - header_size;

	std::lock_guard<std::mutex> lock(wc_mtx);
	if(status == sockstatus_t::LISTENING) {
		if(!hdr->flag_syn || hdr->flag_ack) {
			// not an incoming connection, or confirmation of an outgoing connection
			// TODO: if there is no child socket with this peer IP/port, send a RST
			// to actively terminate the invalid connection. Now, the packets are
			// ignored causing timeouts on the other end.
			return false;
		}
		// Active incoming connection, add to the list
		assert(get_pseudo_fd() == 0);
		tcp_incoming_connection c;
		c.frame = std::string(frame, framelen);
		c.ip_offset = ip_offset;
		c.tcp_offset = tcp_offset;
		c.payload_offset = payload_offset;
		c.payload_length = payload_length;
		waiting_connections.emplace(std::move(c));
		incoming_cv.notify_all();
		return true;
	}

	if(htons(hdr->source_port) != get_peer_port()) {
		return false;
	}

	if(status == sockstatus_t::CONNECTING) {
		if(hdr->flag_syn && hdr->flag_ack) {
			if(htonl(hdr->acknum) != send_seq_num + 1) {
				// TODO: reset & close connection
				dprintf(0, "Got invalid SYN/ACK, dropping\n");
				return true;
			}
			send_seq_num++;
			// incoming connection approval!
			send_ack_num = htonl(hdr->seqnum) + 1;
			send_tcp_frame(false /* syn */, true /* ack */);
			status = sockstatus_t::CONNECTED;
			incoming_cv.notify_all();
		} else {
			dprintf(0, "Got non-SYNACK packet on CONNECTING socket\n");
		}
		return true;
	}

	// this is a connected socket, IPs and ports match, meant for us

	if(hdr->flag_ack) {
		uint32_t acknum = htonl(hdr->acknum);
		// TODO: handle overflow of sequence numbers
		size_t acked_bytes = acknum - send_seq_num;
		send_buffer = send_buffer.substr(acked_bytes);
		send_seq_num = acknum;
	}

	if(payload_length > 0) {
		// extra data
		// TODO: handle out-of-order data
		if(htonl(hdr->seqnum) == send_ack_num) {
			recv_buffer.append(std::string(frame + payload_offset, payload_length));
			// TODO: handle overflow of sequence numbers
			send_ack_num += payload_length;
			send_tcp_frame(false /* syn */, true /* ack */);
			incoming_cv.notify_all();
		} else {
			dprintf(0, "Dropped TCP data because sequence number is not as expected (%d vs %d)\n", htonl(hdr->seqnum), send_ack_num);
		}
	}

	if(hdr->flag_psh) {
		// TODO: send to application immediately, so return from any
		// current read call
	}

	if(hdr->flag_fin) {
		// TODO: implement closing
	}

	return true;
}

std::shared_ptr<tcp_socket> tcp_socket::get_child(cosix::pseudofd_t ps)
{
	std::lock_guard<std::mutex> lock(child_sockets_mtx);
	auto childit = child_sockets.find(ps);
	if(childit == child_sockets.end()) {
		throw cloudabi_system_error(EBADF);
	}
	assert(childit->second->get_pseudo_fd() == ps);
	return childit->second;
}

void tcp_socket::pwrite(pseudofd_t p, off_t o, const char *msg, size_t len)
{
	// I may receive calls for pseudo-sockets I accept()ed, forward them
	if(p != get_pseudo_fd()) {
		get_child(p)->pwrite(p, o, msg, len);
		return;
	}

	if(status == sockstatus_t::LISTENING || status == sockstatus_t::CONNECTING) {
		throw cloudabi_system_error(EINVAL);
	}

	send_buffer.append(std::string(msg, len));
	send_tcp_frame(false /* syn */, true /* ack */);
}

cloudabi_errno_t tcp_socket::establish() {
	std::unique_lock<std::mutex> lock(wc_mtx);
	if(status == sockstatus_t::LISTENING) {
		// nothing to establish
		return 0;
	}
	assert(status == sockstatus_t::CONNECTING);
	arc4random_buf(&send_seq_num, sizeof(send_seq_num));
	send_tcp_frame(true /* syn */, false /* ack */);
	while(status == sockstatus_t::CONNECTING /* or an error or timeout occurred */) {
		// wait for SYN|ACK
		incoming_cv.wait(lock);
	}
	assert(status == sockstatus_t::CONNECTED);
	return 0;
}

void tcp_socket::send_tcp_frame(bool syn, bool ack) {
	// if we're just establishing or confirming, buffer must be empty
	assert(!(syn && send_buffer.size()));

	struct tcp_header tcp_hdr;
	memset(&tcp_hdr, 0, sizeof(tcp_header));
	tcp_hdr.source_port = htons(get_local_port());
	tcp_hdr.dest_port = htons(get_peer_port());
	// TODO: don't re-send the whole buffer all the time, keep track of
	// sent unacked segments, send only unsent data and handle
	// retransmissions properly
	tcp_hdr.seqnum = htonl(send_seq_num);
	tcp_hdr.acknum = htonl(send_ack_num);
	if(syn) tcp_hdr.flag_syn = 1;
	if(ack) tcp_hdr.flag_ack = 1;
	// TODO: don't always set PSH on data, but when though?
	if(!send_buffer.empty()) tcp_hdr.flag_psh = 1;
	// TODO: use a proper growing window
	tcp_hdr.window = htons(128);
	tcp_hdr.data_off = sizeof(tcp_hdr) / 4;

	uint32_t source_ip = *reinterpret_cast<uint32_t const*>(get_local_ip().c_str());
	uint32_t dest_ip = *reinterpret_cast<uint32_t const*>(get_peer_ip().c_str());

	struct ipv4_pseudo_hdr pseudo_hdr;
	memset(&pseudo_hdr, 0, sizeof(pseudo_hdr));
	pseudo_hdr.source_ip = source_ip;
	pseudo_hdr.dest_ip = dest_ip;
	pseudo_hdr.protocol = transport_proto::tcp;
	pseudo_hdr.length = htons(sizeof(tcp_hdr) + send_buffer.size());
	uint16_t *pseudo_hdr_16 = reinterpret_cast<uint16_t*>(&pseudo_hdr);
	uint32_t short_sum = 0;
	for(size_t i = 0; i < sizeof(pseudo_hdr) / 2; ++i) {
		short_sum += pseudo_hdr_16[i];
	}
	uint16_t *tcp_hdr_16 = reinterpret_cast<uint16_t*>(&tcp_hdr);
	for(size_t i = 0; i < sizeof(tcp_hdr) / 2; ++i) {
		short_sum += tcp_hdr_16[i];
	}
	size_t loops = send_buffer.size() / 2 + ((send_buffer.size() % 2) ? 1 : 0);
	for(size_t i = 0; i < loops; ++i) {
		if(i * 2 == send_buffer.size() - 1) {
			short_sum += uint16_t(send_buffer.back());
		} else {
			short_sum += *reinterpret_cast<uint16_t const*>(send_buffer.c_str() + i * 2);
		}
	}
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	tcp_hdr.checksum = ~short_sum;

	struct ip_header ip_hdr;
	memset(&ip_hdr, 0, sizeof(ip_hdr));
	ip_hdr.ihl = 5;
	ip_hdr.version = 4;
	ip_hdr.total_len = htons(sizeof(ip_hdr) + sizeof(tcp_hdr) + send_buffer.size());
	arc4random_buf(&ip_hdr.ident, sizeof(ip_hdr.ident));
	ip_hdr.ttl = 0xff;
	ip_hdr.proto = transport_proto::tcp;
	ip_hdr.source_ip = source_ip;
	ip_hdr.dest_ip = dest_ip;

	uint16_t *ip_hdr_16 = reinterpret_cast<uint16_t*>(&ip_hdr);
	short_sum = 0;
	for(size_t i = 0; i < sizeof(ip_hdr) / 2; ++i) {
		short_sum += ip_hdr_16[i];
	}
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	ip_hdr.checksum = ~short_sum;

	std::vector<iovec> vecs(3);
	vecs[0].iov_base = &ip_hdr;
	vecs[0].iov_len = sizeof(ip_hdr);
	vecs[1].iov_base = &tcp_hdr;
	vecs[1].iov_len = sizeof(tcp_hdr);
	if(send_buffer.empty()) {
		vecs.resize(2);
	} else {
		vecs[2].iov_base = const_cast<char*>(send_buffer.c_str());
		vecs[2].iov_len = send_buffer.size();
	}

	cloudabi_errno_t res;
	try {
		res = get_ip().send_packet(vecs, get_peer_ip());
	} catch(std::runtime_error &e) {
		fprintf(stderr, "Failed to send TCP frame: %s\n", e.what());
		throw cloudabi_system_error(ECANCELED);
	}
	if(res != 0) {
		throw cloudabi_system_error(res);
	}
}

size_t tcp_socket::pread(pseudofd_t p, off_t o, char *dest, size_t requested)
{
	if(p != get_pseudo_fd()) {
		return get_child(p)->pread(p, o, dest, requested);
	}

	if(status == sockstatus_t::LISTENING || status == sockstatus_t::CONNECTING) {
		throw cloudabi_system_error(EINVAL);
	}

	std::unique_lock<std::mutex> lock(wc_mtx);
	while(recv_buffer.empty()) {
		incoming_cv.wait(lock);
	}

	auto res = std::min(recv_buffer.size(), requested);
	memcpy(dest, recv_buffer.c_str(), res);
	recv_buffer = recv_buffer.substr(res);
	return res;
}

pseudofd_t tcp_socket::sock_accept(pseudofd_t pseudo, cloudabi_sockstat_t *ss)
{
	// only the root TCP socket may accept()
	if(pseudo != 0 || status != sockstatus_t::LISTENING) {
		throw cloudabi_system_error(EINVAL);
	}
	assert(get_pseudo_fd() == 0);

	tcp_incoming_connection conn;
	pseudofd_t subsocket;
	{
		std::unique_lock<std::mutex> lock(wc_mtx);
		while(waiting_connections.empty()) {
			incoming_cv.wait(lock);
		}
		conn = waiting_connections.front();
		waiting_connections.pop();
		subsocket = ++last_subsocket;
	}

	auto *ip_hdr = reinterpret_cast<ip_header const*>(conn.frame.c_str() + conn.ip_offset);
	auto *tcp_hdr = reinterpret_cast<tcp_header const*>(conn.frame.c_str() + conn.tcp_offset);
	std::string local_ip = std::string(reinterpret_cast<char const*>(&ip_hdr->dest_ip), sizeof(ip_hdr->dest_ip));
	std::string peer_ip = std::string(reinterpret_cast<char const*>(&ip_hdr->source_ip), sizeof(ip_hdr->source_ip));
	uint16_t peer_port = htons(tcp_hdr->source_port);

	assert(get_local_ip() == std::string(4, 0) || local_ip == get_local_ip());
	assert(get_local_ip() != std::string(4, 0) || local_ip != get_local_ip());
	assert(get_local_port() == htons(tcp_hdr->dest_port));

	std::shared_ptr<tcp_socket> socket = std::make_shared<tcp_socket>(
		local_ip, get_local_port(), peer_ip, peer_port, subsocket, get_reverse_fd());
	get_ip().register_socket(socket);
	socket->send_ack_num = htonl(tcp_hdr->seqnum) + 1;
	arc4random_buf(&socket->send_seq_num, sizeof(socket->send_seq_num));
	socket->send_tcp_frame(true /* syn */, true /* ack */);
	// expect a 1 byte higher ack
	socket->send_seq_num += 1;
	socket->status = sockstatus_t::CONNECTED;

	{
		std::lock_guard<std::mutex> lock(child_sockets_mtx);
		child_sockets[subsocket] = socket;
	}
	socket->sock_stat_get(subsocket, ss);

	// do not start(), the root TCP socket will listen on the reversefd
	return subsocket;
}

void tcp_socket::sock_stat_get(cosix::pseudofd_t p, cloudabi_sockstat_t *ss)
{
	if(p != get_pseudo_fd()) {
		get_child(p)->sock_stat_get(p, ss);
		return;
	}

	ss->ss_sockname.sa_family = AF_INET;
	memcpy(ss->ss_sockname.sa_inet.addr, get_local_ip().c_str(), sizeof(ss->ss_sockname.sa_inet.addr));
	ss->ss_sockname.sa_inet.port = get_local_port();
	ss->ss_peername.sa_family = AF_INET;
	memcpy(ss->ss_peername.sa_inet.addr, get_peer_ip().c_str(), sizeof(ss->ss_peername.sa_inet.addr));
	ss->ss_peername.sa_inet.port = get_peer_port();
	ss->ss_error = 0; /* TODO */
	ss->ss_state = get_peer_ip().empty() ? CLOUDABI_SOCKSTATE_ACCEPTCONN : 0;
}
