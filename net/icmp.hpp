#pragma once

#include "oslibc/error.h"
#include "global.hpp"
#include "hw/vga_stream.hpp"
#include "net/ip.hpp"

namespace cloudos {

struct interface;

struct icmp_implementation {
	cloudabi_errno_t received_ipv4(interface*, uint8_t*, size_t, ipv4addr_t, ipv4addr_t) {
		get_vga_stream() << "Received ICMPv4 packet on some interface, ignoring for now\n";
		return EINVAL;
	}
};

}
