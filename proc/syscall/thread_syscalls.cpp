#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>
#include <fd/scheduler.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_thread_create(syscall_context &c)
{
	auto args = arguments_t<cloudabi_threadattr_t*, cloudabi_tid_t*>(c);
	auto attr = args.first();
	shared_ptr<thread> thr = c.process()->add_thread(attr->stack, attr->stack_len, attr->argument, reinterpret_cast<void*>(attr->entry_point));
	c.result = thr->get_thread_id();
	return 0;
}

cloudabi_errno_t cloudos::syscall_thread_exit(syscall_context &c)
{
	auto args = arguments_t<_Atomic(cloudabi_lock_t)*, cloudabi_scope_t>(c);
	auto lock = args.first();
	auto scope = args.second();
	if(scope != CLOUDABI_SCOPE_PRIVATE) {
		get_vga_stream() << "thread_exit(): non-private locks are not supported yet\n";
		return ENOSYS;
	}
	c.thread->thread_exit();
	c.thread->drop_userspace_lock(lock);
	get_scheduler()->thread_yield();
	kernel_panic("Unreachable");
}

cloudabi_errno_t cloudos::syscall_thread_yield(syscall_context &)
{
	get_scheduler()->thread_yield();
	return 0;
}
