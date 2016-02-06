#pragma once

#include <stdint.h>
#include <stddef.h>
#include "oslibc/error.h"
#include "net/protocol_store.hpp"

namespace cloudos {

struct interface;

typedef uint8_t ipv4addr_t[4];
typedef uint8_t ipv6addr_t[16];

struct ip_implementation {
	error_t received_ip_packet(interface *iface, uint8_t *packet, size_t length);
	error_t send_ipv4_packet(uint8_t *payload, size_t length, ipv4addr_t source, ipv4addr_t destination, protocol_t inner_protocol);

private:
	error_t received_ipv4_packet(interface *iface, uint8_t *packet, size_t length);
	error_t received_ipv6_packet(interface *iface, uint8_t *packet, size_t length);

	// TODO routing table
};

}

