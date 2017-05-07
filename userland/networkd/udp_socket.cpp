#include "udp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>
#include "ip.hpp"

using namespace cosix;
using namespace networkd;

#define MAX_UDP_PACKETS 64

udp_socket::udp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, int fd)
: ip_socket(transport_proto::udp, l_ip, l_p, p_ip, p_p, fd)
{
}

udp_socket::~udp_socket()
{
}

bool udp_socket::handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t /*framelen*/, size_t /*ip_offset*/, size_t udp_offset, size_t udp_length)
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

	if(htons(hdr->source_port) != get_peer_port()
	|| htons(hdr->dest_port) != get_local_port()) {
		// not meant for us
		return false;
	}

	const char *payload = frame + udp_offset + sizeof(udp_header);
	size_t payload_length = udp_length - sizeof(udp_header);

	dprintf(0, "Got a %d byte packet from %s\n", payload_length, iface->get_name().c_str());

	std::lock_guard<std::mutex> lock(wm_mtx);
	if(waiting_messages.size() < MAX_UDP_PACKETS) {
		waiting_messages.emplace(payload, payload_length);
	}
	return true;
}

void udp_socket::pwrite(pseudofd_t p, off_t, const char *msg, size_t len)
{
	if(p != 0) {
		throw cloudabi_system_error(EINVAL);
	}

	if(get_peer_ip().empty()) {
		// not connected, cannot send
		throw cloudabi_system_error(EDESTADDRREQ);
	}

	dprintf(0, "Got a %d byte packet, forwarding onwards.\n", len);
	dprintf(0, "Local port: %d, peer port: %d\n", get_local_port(), get_peer_port());
	std::string ld = ipv4_ntop(get_local_ip());
	std::string pd = ipv4_ntop(get_peer_ip());
	dprintf(0, "Local IP: %s, peer IP: %s\n", ld.c_str(), pd.c_str());

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
	ip_hdr.ident = 0;
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

size_t udp_socket::pread(pseudofd_t p, off_t, char *dest, size_t requested)
{
	dprintf(0, "Pread() size %zu\n", requested);
	if(p != 0) {
		throw cloudabi_system_error(EINVAL);
	}

	std::string message;
	{
		std::unique_lock<std::mutex> lock(wm_mtx);
		while(waiting_messages.empty()) {
			wm_cv.wait(lock);
		}
		message = waiting_messages.front();
		waiting_messages.pop();
	}

	auto res = std::min(message.size(), requested);
	memcpy(dest, message.c_str(), res);
	dprintf(0, "pread(): Returning a %zu size message\n", res);
	return res;
}
