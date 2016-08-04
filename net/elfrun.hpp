#pragma once

#include <stdint.h>
#include <stddef.h>
#include "oslibc/error.h"
#include "net/udp.hpp"

namespace cloudos {

struct interface;

struct elfrun_implementation : public udp_listener {
	elfrun_implementation();

	error_t received_udp4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t source, uint16_t sourceport, ipv4addr_t destination, uint16_t destport) override;

private:
	error_t run_binary();

	uint8_t buffer[32384];
	size_t pos;
	bool awaiting;
};

}
