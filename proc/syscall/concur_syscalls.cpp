#include <proc/syscalls.hpp>
#include <fd/thread.hpp>
#include <global.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_condvar_signal(syscall_context &c) {
	auto args = arguments_t<_Atomic(cloudabi_condvar_t)*, cloudabi_scope_t, cloudabi_nthreads_t>(c);
	auto condvar = args.first();
	auto scope = args.second();
	auto nwaiters = args.third();
	if(scope != CLOUDABI_SCOPE_PRIVATE) {
		get_vga_stream() << "condvar_signal(): non-private condition variables are not supported yet\n";
		return ENOSYS;
	}

	c.thread->signal_userspace_cv(condvar, nwaiters);
	return 0;
}

cloudabi_errno_t cloudos::syscall_lock_unlock(syscall_context &c) {
	auto args = arguments_t<_Atomic(cloudabi_lock_t)*, cloudabi_scope_t>(c);
	auto lock = args.first();
	auto scope = args.second();
	if(scope != CLOUDABI_SCOPE_PRIVATE) {
		get_vga_stream() << "lock_unlock(): non-private locks are not supported yet\n";
		return ENOSYS;
	}

	c.thread->drop_userspace_lock(lock);
	return 0;
}
