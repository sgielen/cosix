#pragma once

#include "oslibc/error.h"
#include "global.hpp"
#include "hw/vga_stream.hpp"
#include "net/ip.hpp"

namespace cloudos {

struct interface;

struct tcp_implementation {
	error_t received_ipv4(interface*, uint8_t*, size_t, ipv4addr_t, ipv4addr_t) {
		// TODO
		get_vga_stream() << "Received TCPv4 packet on some interface, ignoring for now\n";
		return error_t::invalid_argument;
	}

private:
	// TODO: TCP table containing connection info and handler lambda
};

}
