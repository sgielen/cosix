#pragma once
#include "net/interface.hpp"

namespace cloudos {

struct loopback_interface : public interface
{
	loopback_interface() : interface(hwtype_t::loopback) {}

	inline virtual cloudabi_errno_t send_packet(uint8_t *packet, size_t length) override {
		return received_ip_packet(packet, length, protocol_t::ip, 0);
	}
};

}
