#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>
#include <fd/scheduler.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_proc_exec(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const void*, size_t, const cloudabi_fd_t*, size_t>(c);
	auto fd = args.first();
	auto fds = args.fourth();
	auto fdslen = args.fifth();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fd, CLOUDABI_RIGHT_PROC_EXEC);
	if(res != 0) {
		return res;
	}

	fd_mapping_t *new_fds[fdslen];
	for(size_t i = 0; i < fdslen; ++i) {
		fd_mapping_t *old_mapping;
		res = c.process()->get_fd(&old_mapping, fds[i], 0);
		if(res != 0) {
			// request to map an invalid fd
			return res;
		}
		// copy the mapping to the new process
		new_fds[i] = old_mapping;
	}

	auto data = args.second();
	auto datalen = args.third();
	res = c.process()->exec(mapping->fd, fdslen, new_fds, data, datalen);
	if(res != 0) {
		get_vga_stream() << "exec() failed because of " << res << "\n";
		return res;
	}
	assert(c.thread->is_exited());
	get_scheduler()->thread_yield();
	kernel_panic("Unreachable code");
}

cloudabi_errno_t cloudos::syscall_proc_exit(syscall_context &c)
{
	auto args = arguments_t<cloudabi_exitcode_t>(c);
	c.process()->exit(args.first());
	// exited will be true after this, so we won't be rescheduled.
	// we'll be cleaned up when the last file descriptor to this process closes.
	get_scheduler()->thread_yield();
	kernel_panic("Unreachable code");
}

cloudabi_errno_t cloudos::syscall_proc_fork(syscall_context &c)
{
	// cloudabi_sys_proc_fork() takes no arguments, creates a new process, and:
	// * in the parent, returns ebx=child_fd, ecx=undefined
	// * in the child, returns ebx=CLOUDABI_PROCESS_CHILD, ecx=MAIN_THREAD
	auto newprocess = make_shared<process_fd>("initializing process");

	newprocess->install_page_directory();
	newprocess->fork(c.thread->shared_from_this());

	c.process()->install_page_directory();

	// set return values for parent
	auto fdnum = c.process()->add_fd(newprocess, CLOUDABI_RIGHT_POLL_PROC_TERMINATE, 0);
	c.set_results(0, fdnum);
	return 0;
}

cloudabi_errno_t cloudos::syscall_proc_raise(syscall_context &c)
{
	auto args = arguments_t<cloudabi_signal_t>(c);
	c.process()->signal(args.first());

	// like with exit, if signal is fatal exited will be true, we'll be cleaned up later
	get_scheduler()->thread_yield();
	return 0;
}

