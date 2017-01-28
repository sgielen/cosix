#include <proc/syscalls.hpp>
#include <global.hpp>
#include <time/clock_store.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_clock_time_get(syscall_context &c) {
	auto args = arguments_t<cloudabi_clockid_t, cloudabi_timestamp_t, cloudabi_timestamp_t*>(c);
	auto clockid = args.first();
	auto precision = args.second();

	auto clock = get_clock_store()->get_clock(clockid);
	if(!clock) {
		get_vga_stream() << "Unknown clock ID " << clockid << "\n";
		return EINVAL;
	}

	c.result = clock->get_time(precision);
	return 0;
}

cloudabi_errno_t cloudos::syscall_clock_res_get(syscall_context &c) {
	auto args = arguments_t<cloudabi_clockid_t, cloudabi_timestamp_t*>(c);
	auto clockid = args.first();

	auto clock = get_clock_store()->get_clock(clockid);
	if(!clock) {
		get_vga_stream() << "Unknown clock ID " << clockid << "\n";
		return EINVAL;
	}

	c.result = clock->get_resolution();
	return 0;
}
