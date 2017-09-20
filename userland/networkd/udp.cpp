#include "udp.hpp"
#include "ip_socket.hpp"
#include "networkd.hpp"
#include "routing_table.hpp"
#include <cassert>

using namespace networkd;

udp::udp() {}

void udp::handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t udp_offset, size_t udp_length)
{
	if(udp_length < sizeof(udp_header)) {
		// UDP header doesn't fit
		return;
	}

	auto *hdr = reinterpret_cast<udp_header const*>(frame + udp_offset);
	if(htons(hdr->length) < udp_length) {
		// UDP payload doesn't fit
		return;
	}

	// if a socket exists for this destination port, send it there
	// TODO: allow binding on an interface as well
	// TODO: if somebody is listening on Flower, ingress a socket?

	std::shared_ptr<udp_socket> recv_socket;
	{
		std::lock_guard<std::mutex> lock(sockets_mtx);
		auto it = sockets.find(ntohs(hdr->dest_port));
		if(it != sockets.end()) {
			recv_socket = it->second;
		}
	}

	if(recv_socket) {
		recv_socket->handle_packet(iface, frame, framelen, ip_offset, udp_offset, udp_length);
	}
}

cloudabi_errno_t udp::register_socket(std::shared_ptr<udp_socket> socket)
{
	std::lock_guard<std::mutex> lock(sockets_mtx);
	if(sockets.find(socket->get_local_port()) != sockets.end()) {
		return EADDRINUSE;
	}

	sockets[socket->get_local_port()] = socket;
	return 0;
}
