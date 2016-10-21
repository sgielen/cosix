#pragma once

#include <stdint.h>
#include <stddef.h>
#include "oslibc/error.h"
#include "net/udp.hpp"

namespace cloudos {

struct interface;

template <typename T>
T elf_endian(T value, uint8_t elf_data) {
	// TODO: know whether we are big or little endian
	if(elf_data == 1) return value;
	T r;
	uint8_t *net = reinterpret_cast<uint8_t*>(&value);
	uint8_t *res = reinterpret_cast<uint8_t*>(&r);
	for(size_t i = 0; i < sizeof(T); ++i) {
		res[i] = net[sizeof(T)-i-1];
	}
	return r;
}

struct elfrun_implementation : public udp_listener {
	elfrun_implementation();

	cloudabi_errno_t received_udp4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t source, uint16_t sourceport, ipv4addr_t destination, uint16_t destport) override;

private:
	cloudabi_errno_t run_binary();

	size_t buffer_size;
	uint8_t *buffer;
	size_t pos;
	bool awaiting;
};

}
