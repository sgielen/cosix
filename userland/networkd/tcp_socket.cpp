#include "tcp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>
#include "ip.hpp"
#include "arp.hpp"
#include <cassert>

using namespace cosix;
using namespace networkd;

#define TCP_SEGMENT_ACK_TIMEOUT 5ull * 1000 * 1000 * 1000 /* 5 seconds */

tcp_socket::tcp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, int r)
: ip_socket(transport_proto::tcp, l_ip, l_p, p_ip, p_p, r)
, status(sockstatus_t::CONNECTING)
{
	arc4random_buf(&send_seq_num, sizeof(send_seq_num));
}

tcp_socket::~tcp_socket()
{
}

bool tcp_socket::handle_packet(std::shared_ptr<interface>, const char *frame, size_t, size_t, size_t tcp_offset, size_t tcp_length)
{
	if(tcp_length < sizeof(tcp_header)) {
		// TCP header doesn't fit
		return false;
	}
	auto *hdr = reinterpret_cast<tcp_header const*>(frame + tcp_offset);

	assert(htons(hdr->dest_port) == get_local_port());
	assert(htons(hdr->source_port) == get_peer_port());

	uint16_t header_size = hdr->data_off * 4;
	if(header_size < sizeof(tcp_header)) {
		// TCP header including options don't fit
		return false;
	}
	size_t payload_offset = tcp_offset + header_size;
	size_t payload_length = tcp_length - header_size;

	std::lock_guard<std::mutex> lock(wc_mtx);

	// TODO: handle out-of-order data
	if(!hdr->flag_syn && htonl(hdr->seqnum) != send_ack_num) {
		dprintf(0, "Dropped TCP data because sequence number is not as expected (%d vs %d)\n", htonl(hdr->seqnum), send_ack_num);
	}

	if(status == sockstatus_t::CONNECTING) {
		if(hdr->flag_syn && hdr->flag_ack) {
			if(htonl(hdr->acknum) != send_seq_num) {
				dprintf(0, "Got invalid SYN/ACK, dropping\n");
				send_tcp_frame(false, false, std::string(), false, true /* rst */);
				status = sockstatus_t::CLOSED;
				becomes_readable();
				return true;
			}
			// incoming connection approval!
			send_ack_num = htonl(hdr->seqnum) + 1;
			received_acknum(htonl(hdr->acknum));
			send_tcp_frame(false /* syn */, true /* ack */);
			status = sockstatus_t::CONNECTED;
			becomes_readable();
		} else if(hdr->flag_syn) {
			// Getting a SYN onto a connecting socket is legal. It means that
			// two machines were connecting to each other simultaneously. Send
			// SYN|ACK instead.
			send_ack_num = htonl(hdr->seqnum) + 1;
			send_tcp_frame(true /* syn */, true /* ack */);
			send_seq_num++;
			status = sockstatus_t::CONNECTED;
			becomes_readable();
		} else if(hdr->flag_rst) {
			// Connection rejected
			status = sockstatus_t::CLOSED;
			becomes_readable();
		} else {
			dprintf(0, "Got non-SYN/RST packet on CONNECTING socket\n");
			if(hdr->flag_syn) dprintf(0, "  (SYN is set)\n");
			if(hdr->flag_ack) dprintf(0, "  (ACK is set)\n");
			if(hdr->flag_rst) dprintf(0, "  (RST is set)\n");
			if(hdr->flag_fin) dprintf(0, "  (FIN is set)\n");
			send_tcp_frame(false, false, std::string(), false, true /* rst */);
			status = sockstatus_t::CLOSED;
			becomes_readable();
		}
		return true;
	}

	assert(status == sockstatus_t::CONNECTED || status == sockstatus_t::THEIRS_CLOSED
	|| status==sockstatus_t::OURS_CLOSED || status == sockstatus_t::CLOSED);

	// this is a connected socket, IPs and ports match, meant for us
	if(hdr->flag_ack) {
		received_acknum(htonl(hdr->acknum));
		pump_segment_queue();
	}

	if(payload_length > 0) {
		// extra data
		recv_buffer.append(std::string(frame + payload_offset, payload_length));
		// TODO: handle overflow of sequence numbers
		send_ack_num += payload_length;
		send_tcp_frame(false /* syn */, true /* ack */);
		becomes_readable();
	}

	if(hdr->flag_psh) {
		// TODO: send to application immediately, so return from any
		// current read call
	}

	if(hdr->flag_rst) {
		// Break down this connection after handling the last data
		status = sockstatus_t::CLOSED;
		becomes_readable();
		return true;
	}

	if(hdr->flag_fin) {
		// other side is closing their part of the connection
		if(status == sockstatus_t::CONNECTED) {
			status = sockstatus_t::THEIRS_CLOSED;
		} else {
			status = sockstatus_t::CLOSED;
		}
		send_ack_num += 1;
		send_tcp_frame(false, true /* ack */);
		becomes_readable();
		return true;
	}

	return true;
}

void tcp_socket::pwrite(pseudofd_t p, off_t, const char *msg, size_t len)
{
	assert(p == 0);

	if(status != sockstatus_t::CONNECTED && status != sockstatus_t::SHUTDOWN) {
		bool ours_closed = status == sockstatus_t::CLOSED || status == sockstatus_t::OURS_CLOSED;
		throw cloudabi_system_error(ours_closed ? ECONNRESET : EINVAL);
	}

	send_tcp_frame(false /* syn */, true /* ack */, std::string(msg, len));
}

cloudabi_errno_t tcp_socket::establish() {
	std::unique_lock<std::mutex> lock(wc_mtx);
	assert(status == sockstatus_t::CONNECTING);
	send_tcp_frame(true /* syn */, false /* ack */);
	send_seq_num++;
	while(status == sockstatus_t::CONNECTING) {
		// wait for SYN|ACK
		incoming_cv.wait(lock);
	}
	assert(status == sockstatus_t::CONNECTED || status == sockstatus_t::CLOSED);
	return status == sockstatus_t::CONNECTED ? 0 : ECONNREFUSED;
}

void tcp_socket::send_tcp_frame(bool syn, bool ack, std::string data, bool fin, bool rst) {
	// if we're establishing, closing or resetting, don't send any data
	assert(! ((syn || fin || rst) && !data.empty()));

	// TODO: instead of hardcoding MTU, request it from interface or use
	// max MSS option from the sender (also, don't assume ethernet frame
	// will be used eventually)
	size_t max_mss = 1500 - sizeof(tcp_header) - sizeof(ip_header) - 18 /* eth frame + checksum */;

	do {
		size_t segment_size = std::min(max_mss, data.size());

		struct tcp_header tcp_hdr;
		memset(&tcp_hdr, 0, sizeof(tcp_header));
		tcp_hdr.source_port = htons(get_local_port());
		tcp_hdr.dest_port = htons(get_peer_port());
		tcp_hdr.seqnum = htonl(send_seq_num);
		tcp_hdr.acknum = htonl(send_ack_num);
		if(syn) tcp_hdr.flag_syn = 1;
		if(ack) tcp_hdr.flag_ack = 1;
		if(fin) tcp_hdr.flag_fin = 1;
		if(rst) tcp_hdr.flag_rst = 1;
		// TODO: don't always set PSH on data, but when though?
		if(segment_size > 0) tcp_hdr.flag_psh = 1;
		// TODO: use a proper growing window
		tcp_hdr.window = htons(128);
		tcp_hdr.data_off = sizeof(tcp_hdr) / 4;

		compute_tcp_checksum(tcp_hdr, get_local_ip(), get_peer_ip(), data, segment_size);

		struct ip_header ip_hdr;
		memset(&ip_hdr, 0, sizeof(ip_hdr));
		ip_hdr.ihl = 5;
		ip_hdr.version = 4;
		ip_hdr.total_len = htons(sizeof(ip_hdr) + sizeof(tcp_hdr) + segment_size);
		arc4random_buf(&ip_hdr.ident, sizeof(ip_hdr.ident));
		ip_hdr.ttl = 0xff;
		ip_hdr.proto = transport_proto::tcp;
		ip_hdr.source_ip = *reinterpret_cast<uint32_t const*>(get_local_ip().c_str());
		ip_hdr.dest_ip = *reinterpret_cast<uint32_t const*>(get_peer_ip().c_str());

		compute_ip_checksum(ip_hdr);

		tcp_outgoing_segment segment;
		segment.segment.reserve(sizeof(ip_hdr) + sizeof(tcp_hdr) + segment_size);
		segment.segment.append(reinterpret_cast<const char*>(&ip_hdr), sizeof(ip_hdr));
		segment.segment.append(reinterpret_cast<const char*>(&tcp_hdr), sizeof(tcp_hdr));
		segment.segment.append(data.c_str(), segment_size);
		segment.seqnum = ntohl(tcp_hdr.seqnum);
		assert(segment_size < UINT16_MAX);
		segment.segsize = segment_size;
		segment.ack_deadline = 0;

		// if it's an empty ACK packet, don't require reliable transport; just fire it off
		// because ACKs themselves aren't ACKed
		bool reliable = !data.empty() || syn || fin;

		// TODO: don't create segments if send_window_size would grow beyond
		// the remote window size; instead, keep extra data in a buffer that is
		// turned into segments as soon as the remote side ACKs data

		if(reliable) {
			outgoing_segments.emplace_back(std::move(segment));
		} else {
			cloudabi_errno_t res;
			try {
				std::vector<iovec> vecs(1);
				vecs[0].iov_base = const_cast<void*>(reinterpret_cast<const void*>(segment.segment.c_str()));
				vecs[0].iov_len = segment.segment.size();
				res = get_ip().send_packet(vecs, get_peer_ip());
			} catch(std::exception &e) {
				fprintf(stderr, "Failed to unreliably send TCP frame: %s\n", e.what());
				res = ECANCELED;
			}
			if(res != 0) {
				fprintf(stderr, "Failed to unreliably send TCP frame: %s\n", strerror(errno));
			}
		}

		send_window_size += segment_size;
		send_seq_num += segment_size;
		data = data.substr(segment_size);
	} while(!data.empty());

	pump_segment_queue();
}

void tcp_socket::received_acknum(uint32_t acknum)
{
	// TODO: handle overflow of sequence numbers
	while(!outgoing_segments.empty() && (outgoing_segments.front().seqnum + outgoing_segments.front().segsize <= acknum)) {
		// first segment is acked! take it out of the segment list
		send_window_size -= outgoing_segments.front().segsize;
		outgoing_segments.pop_front();
	}
}

void tcp_socket::pump_segment_queue()
{
	uint32_t last_seqnum = 0;
	auto now = monotime();
	cloudabi_errno_t res = 0;
	for(auto &segment : outgoing_segments) {
		// TODO: handle overflow
		assert(segment.seqnum > last_seqnum);

		if(segment.ack_deadline > now) {
			continue;
		}

		try {
			std::vector<iovec> vecs(1);
			vecs[0].iov_base = const_cast<void*>(reinterpret_cast<const void*>(segment.segment.c_str()));
			vecs[0].iov_len = segment.segment.size();
			res = get_ip().send_packet(vecs, get_peer_ip());
			if(res != 0) {
				break;
			}
			// TODO: use an exponential back-off timer for every segment
			segment.ack_deadline = now + TCP_SEGMENT_ACK_TIMEOUT;
		} catch(std::runtime_error &e) {
			fprintf(stderr, "Failed to send TCP frame: %s\n", e.what());
			res = ECANCELED;
			break;
		}
	}

	next_ack_deadline = UINT64_MAX;
	if(res != 0) {
		fprintf(stderr, "Failed to send TCP frame: %s\n", strerror(res));
		return;
	}

	for(auto const &segment : outgoing_segments) {
		assert(segment.ack_deadline != 0);
		next_ack_deadline = std::min(segment.ack_deadline, next_ack_deadline);
	}
}

size_t tcp_socket::pread(pseudofd_t p, off_t, char *dest, size_t requested)
{
	assert(p == 0);

	if(requested == 0) {
		throw cloudabi_system_error(EMSGSIZE);
	}

	if(status != sockstatus_t::CONNECTED && status != sockstatus_t::SHUTDOWN) {
		bool theirs_closed = status == sockstatus_t::CLOSED || status == sockstatus_t::THEIRS_CLOSED;
		throw cloudabi_system_error(theirs_closed ? ECONNRESET : EINVAL);
	}

	std::unique_lock<std::mutex> lock(wc_mtx);
	while(recv_buffer.empty() && (status == sockstatus_t::CONNECTED || status == sockstatus_t::OURS_CLOSED)) {
		incoming_cv.wait(lock);
	}

	auto res = std::min(recv_buffer.size(), requested);
	memcpy(dest, recv_buffer.c_str(), res);
	recv_buffer = recv_buffer.substr(res);
	return res;
}

bool tcp_socket::is_readable(cosix::pseudofd_t p)
{
	assert(p == 0);

	std::unique_lock<std::mutex> lock(wc_mtx);
	return !recv_buffer.empty();
}

void tcp_socket::timed_out()
{
	std::unique_lock<std::mutex> lock(wc_mtx);
	pump_segment_queue();
}

cloudabi_timestamp_t tcp_socket::next_timeout()
{
	return next_ack_deadline;
}

void tcp_socket::becomes_readable()
{
	ip_socket::becomes_readable();
	incoming_cv.notify_all();
}
