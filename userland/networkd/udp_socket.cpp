#include "udp_socket.hpp"
#include "interface.hpp"
#include <arpa/inet.h>

using namespace networkd;

udp_socket::udp_socket(std::string l_ip, uint16_t l_p, std::string p_ip, uint16_t p_p, int fd)
: ip_socket(transport_proto::udp, l_ip, l_p, p_ip, p_p, fd)
{
}

bool udp_socket::handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t /*framelen*/, size_t /*ip_offset*/, size_t payload_offset, size_t payload_length)
{
	if(payload_length < sizeof(udp_header)) {
		// UDP header doesn't fit
		return false;
	}
	auto *hdr = reinterpret_cast<udp_header const*>(frame + payload_offset);
	if(htons(hdr->length) < payload_length) {
		// UDP payload doesn't fit
		return false;
	}

	if(htons(hdr->source_port) != get_local_port()
	|| htons(hdr->dest_port) != get_peer_port()) {
		// not meant for us
		return false;
	}

	// TODO: send it to the pseudo FD
	dprintf(0, "Got a %d byte packet from %s\n", payload_length, iface->get_name().c_str());
	return true;
}

void udp_socket::start()
{
	// TODO: start a thread that will listen for requests from the pseudo fd
}
