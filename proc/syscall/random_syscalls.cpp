#include <proc/syscalls.hpp>
#include <global.hpp>
#include <rng/rng.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_random_get(syscall_context &c)
{
	auto args = arguments_t<char*, size_t>(c);
	auto buf = args.first();
	auto nbyte = args.second();
	get_random()->get(buf, nbyte);
	return 0;
}
