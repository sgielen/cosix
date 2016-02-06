#pragma once

#include <stdint.h>
#include <stddef.h>
#include "oslibc/error.h"
#include "net/ip.hpp"

namespace cloudos {

struct interface;

struct udp_listener {
	virtual ~udp_listener() {}
	virtual error_t received_udp4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t source, uint16_t sourceport, ipv4addr_t destination, uint16_t destport) = 0;
};

struct udp_implementation {
	error_t received_ipv4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t source, ipv4addr_t destination);

	error_t send_ipv4_udp(const uint8_t *payload, size_t length, ipv4addr_t source, uint16_t sourceport, ipv4addr_t destination, uint16_t destport);

private:
	// TODO: UDP table (interface/IP+port -> list of lambdas)
};

}
