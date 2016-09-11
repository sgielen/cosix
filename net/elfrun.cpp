#include "net/elfrun.hpp"
#include "global.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/string.h"
#include "memory/allocator.hpp"
#include "fd/process_fd.hpp"
#include "fd/scheduler.hpp"

using namespace cloudos;

elfrun_implementation::elfrun_implementation()
: buffer_size(10 * 1024 * 1024)
, buffer(get_allocator()->allocate<uint8_t>(buffer_size))
, pos(0)
, awaiting(false)
{
}

error_t elfrun_implementation::run_binary() {
	process_fd *process = get_allocator()->allocate<process_fd>();
	new(process) process_fd("elfrun process");
	auto res = process->exec(buffer, pos, nullptr, 0);
	if(res != error_t::no_error) {
		return res;
	}
	return error_t::no_error;
}

error_t elfrun_implementation::received_udp4(interface*, uint8_t *payload, size_t length, ipv4addr_t, uint16_t, ipv4addr_t, uint16_t)
{
	bool first_packet = payload[0] & 0x01;
	bool last_packet = payload[0] & 0x02;

	if(first_packet) {
		pos = 0;
		awaiting = true;
	} else if(!awaiting) {
		get_vga_stream() << "  elfrun: ignoring out-of-stream packet\n";
		return error_t::no_error;
	}

	if(pos + length - 1 > buffer_size) {
		get_vga_stream() << "  elfrun: downloading this binary would cause buffer overflow, ignoring\n";
		awaiting = false;
		return error_t::no_error;
	}

	memcpy(buffer + pos, payload + 1, length - 1);
	pos += length - 1;

	if(last_packet) {
		get_vga_stream() << "  elfrun: transmission complete, running binary\n";
		auto res = run_binary();
		if(res != error_t::no_error) {
			get_vga_stream() << "  elfrun: binary failed: " << res << "\n";
		}
		awaiting = false;
	}

	return error_t::no_error;
}
