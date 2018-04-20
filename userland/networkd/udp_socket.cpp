#include "udp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>
#include "ip.hpp"
#include <cassert>

using namespace cosix;
using namespace networkd;

#define MAX_UDP_PACKETS 64

udp_socket::udp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, int r)
: ip_socket(transport_proto::udp, l_ip, l_p, p_ip, p_p, r)
{
}

udp_socket::~udp_socket()
{
}

bool udp_socket::handle_packet(std::shared_ptr<interface>, const char *frame, size_t framelen, size_t ip_offset, size_t udp_offset, size_t udp_length)
{
	if(udp_length < sizeof(udp_header)) {
		// UDP header doesn't fit
		return false;
	}
	auto *hdr = reinterpret_cast<udp_header const*>(frame + udp_offset);
	if(htons(hdr->length) < udp_length) {
		// UDP payload doesn't fit
		return false;
	}

	if((!get_peer_ip().empty() && htons(hdr->source_port) != get_peer_port())
	|| htons(hdr->dest_port) != get_local_port()) {
		// not meant for us
		return false;
	}

	size_t payload_length = udp_length - sizeof(udp_header);

	std::lock_guard<std::mutex> lock(wm_mtx);
	if(waiting_messages.size() < MAX_UDP_PACKETS) {
		udp_message m;
		m.frame = std::string(frame, framelen);
		m.ip_offset = ip_offset;
		m.udp_offset = udp_offset;
		m.payload_offset = udp_offset + sizeof(udp_header);
		m.payload_length = payload_length;
		waiting_messages.emplace(std::move(m));
		becomes_readable();
		wm_cv.notify_all();
	}
	return true;
}

void udp_socket::pwrite(pseudofd_t p, off_t, const char *msg, size_t len)
{
	return sock_send(p, msg, len);
}

void udp_socket::sock_send(pseudofd_t p, const char *msg, size_t len)
{
	(void)p;
	assert(p == 0);

	if(get_peer_ip().empty()) {
		// not connected, cannot send
		throw cloudabi_system_error(EDESTADDRREQ);
	}

	struct udp_header udp_hdr;
	udp_hdr.source_port = htons(get_local_port());
	udp_hdr.dest_port = htons(get_peer_port());
	udp_hdr.length = htons(sizeof(udp_hdr) + len);
	udp_hdr.checksum = 0; /* TODO compute checksum */

	struct ip_header ip_hdr;
	memset(&ip_hdr, 0, sizeof(ip_hdr));
	ip_hdr.ihl = 5;
	ip_hdr.version = 4;
	ip_hdr.total_len = htons(sizeof(ip_hdr) + sizeof(udp_hdr) + len);
	arc4random_buf(&ip_hdr.ident, sizeof(ip_hdr.ident));
	ip_hdr.ttl = 0xff;
	ip_hdr.proto = transport_proto::udp;
	ip_hdr.source_ip = *reinterpret_cast<uint32_t const*>(get_local_ip().c_str());
	ip_hdr.dest_ip = *reinterpret_cast<uint32_t const*>(get_peer_ip().c_str());

	uint16_t *ip_hdr_16 = reinterpret_cast<uint16_t*>(&ip_hdr);
	uint32_t short_sum = 0;
	for(size_t i = 0; i < sizeof(ip_hdr) / 2; ++i) {
		short_sum += ip_hdr_16[i];
	}
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	ip_hdr.checksum = ~short_sum;

	std::vector<iovec> vecs(3);
	vecs[0].iov_base = &ip_hdr;
	vecs[0].iov_len = sizeof(ip_hdr);
	vecs[1].iov_base = &udp_hdr;
	vecs[1].iov_len = sizeof(udp_hdr);
	vecs[2].iov_base = const_cast<char*>(msg);
	vecs[2].iov_len = len;

	cloudabi_errno_t res;
	try {
		res = get_ip().send_packet(vecs, get_peer_ip());
	} catch(std::runtime_error &e) {
		fprintf(stderr, "Failed to send UDP packet: %s\n", e.what());
		throw cloudabi_system_error(ECANCELED);
	}
	if(res != 0) {
		throw cloudabi_system_error(res);
	}
}

size_t udp_socket::pread(pseudofd_t p, off_t, char *dest, size_t requested)
{
	return sock_recv(p, dest, requested);
}

size_t udp_socket::sock_recv(pseudofd_t p, char *dest, size_t requested)
{
	(void)p;
	assert(p == 0);

	udp_message message;
	{
		std::unique_lock<std::mutex> lock(wm_mtx);
		while(waiting_messages.empty()) {
			wm_cv.wait(lock);
		}
		message = waiting_messages.front();
		waiting_messages.pop();
	}

	auto res = std::min(message.payload_length, requested);
	memcpy(dest, message.frame.c_str() + message.payload_offset, res);
	return res;
}

bool udp_socket::is_readable(cosix::pseudofd_t p, size_t &nbytes, bool &hangup)
{
	(void)p;
	assert(p == 0);

	std::unique_lock<std::mutex> lock(wm_mtx);
	bool readable = !waiting_messages.empty();
	if(readable) {
		nbytes = waiting_messages.front().payload_length;
		hangup = false;
	}
	return readable;
}

void udp_socket::close(cosix::pseudofd_t p)
{
	(void)p;
	assert(p == 0);
	std::unique_lock<std::mutex> lock(wm_mtx);
	{
		decltype(waiting_messages) empty_queue;
		std::swap(waiting_messages, empty_queue);
	}
	assert(waiting_messages.empty());
	get_ip().get_udp_impl().unregister_socket(std::dynamic_pointer_cast<udp_socket>(shared_from_this()));
	stop();
}
