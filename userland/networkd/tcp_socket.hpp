#pragma once
#include "ip_socket.hpp"

namespace networkd {

struct tcp_socket : public ip_socket {
	tcp_socket(std::string local_ip, uint16_t local_port, std::string peer_ip, uint16_t peer_port, int fd);

	bool handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t payload_offset, size_t payload_length) override;

private:
};

}
