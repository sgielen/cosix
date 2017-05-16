#include "udp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>
#include "ip.hpp"
#include <cassert>

using namespace cosix;
using namespace networkd;

#define MAX_UDP_PACKETS 64

udp_socket::udp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, pseudofd_t ps, int r)
: ip_socket(transport_proto::udp, l_ip, l_p, p_ip, p_p, ps, r)
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
		wm_cv.notify_all();
	}
	return true;
}

std::shared_ptr<udp_socket> udp_socket::get_child(cosix::pseudofd_t ps)
{
	std::lock_guard<std::mutex> lock(child_sockets_mtx);
	auto childit = child_sockets.find(ps);
	if(childit == child_sockets.end()) {
		throw cloudabi_system_error(EBADF);
	}
	assert(childit->second->get_pseudo_fd() == ps);
	return childit->second;
}

void udp_socket::pwrite(pseudofd_t p, off_t o, const char *msg, size_t len)
{
	// I may receive calls for pseudo-sockets I accept()ed, forward them
	if(p != get_pseudo_fd()) {
		get_child(p)->pwrite(p, o, msg, len);
		return;
	}

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
	ip_hdr.ihl = 5;
	ip_hdr.version = 4;
	ip_hdr.tos = 0;
	ip_hdr.total_len = htons(sizeof(ip_hdr) + sizeof(udp_hdr) + len);
	arc4random_buf(&ip_hdr.ident, sizeof(ip_hdr.ident));
	ip_hdr.frag_offset = 0;
	ip_hdr.flags = 0;
	ip_hdr.ttl = 0xff;
	ip_hdr.proto = transport_proto::udp;
	ip_hdr.checksum = 0;
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

size_t udp_socket::pread(pseudofd_t p, off_t o, char *dest, size_t requested)
{
	if(p != get_pseudo_fd()) {
		return get_child(p)->pread(p, o, dest, requested);
	}

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

pseudofd_t udp_socket::sock_accept(pseudofd_t pseudo, cloudabi_sockstat_t *ss)
{
	// only the root UDP socket may accept()
	if(pseudo != 0) {
		throw cloudabi_system_error(EINVAL);
	}
	assert(get_pseudo_fd() == 0);

	// TODO: send all existing traffic towards newly accepted sockets?
	// for now, just create a new socket for every message
	udp_message message;
	pseudofd_t subsocket;
	{
		std::unique_lock<std::mutex> lock(wm_mtx);
		while(waiting_messages.empty()) {
			wm_cv.wait(lock);
		}
		message = waiting_messages.front();
		waiting_messages.pop();
		subsocket = ++last_subsocket;
	}

	auto *ip_hdr = reinterpret_cast<ip_header const*>(message.frame.c_str() + message.ip_offset);
	auto *udp_hdr = reinterpret_cast<udp_header const*>(message.frame.c_str() + message.udp_offset);
	std::string local_ip = std::string(reinterpret_cast<char const*>(&ip_hdr->dest_ip), sizeof(ip_hdr->dest_ip));
	std::string peer_ip = std::string(reinterpret_cast<char const*>(&ip_hdr->source_ip), sizeof(ip_hdr->source_ip));
	uint16_t peer_port = htons(udp_hdr->source_port);

	assert(get_local_ip() == std::string(4, 0) || local_ip == get_local_ip());
	assert(get_local_ip() != std::string(4, 0) || local_ip != get_local_ip());
	assert(get_local_port() == htons(udp_hdr->dest_port));

	std::shared_ptr<udp_socket> socket = std::make_shared<udp_socket>(
		local_ip, get_local_port(), peer_ip, peer_port, subsocket, get_reverse_fd());
	socket->waiting_messages.emplace(std::move(message));

	{
		std::lock_guard<std::mutex> lock(child_sockets_mtx);
		child_sockets[subsocket] = socket;
	}
	socket->sock_stat_get(subsocket, ss);
	get_ip().register_socket(socket);

	// do not start(), the root UDP socket will listen on the reversefd
	return subsocket;
}

void udp_socket::sock_stat_get(cosix::pseudofd_t p, cloudabi_sockstat_t *ss)
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
