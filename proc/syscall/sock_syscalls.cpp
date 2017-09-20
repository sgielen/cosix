#include <proc/syscalls.hpp>
#include <fd/process_fd.hpp>
#include <global.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_sock_recv(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_recv_in_t*, cloudabi_recv_out_t*>(c);
	auto fdnum = args.first();
	auto recv_in = args.second();
	auto recv_out = args.third();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ);
	if(res != 0) {
		return res;
	}

	mapping->fd->sock_recv(recv_in, recv_out);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_sock_send(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_send_in_t*, cloudabi_send_out_t*>(c);
	auto fdnum = args.first();
	auto send_in = args.second();
	auto send_out = args.third();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE);
	if(res != 0) {
		return res;
	}

	mapping->fd->sock_send(send_in, send_out);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_sock_shutdown(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_sdflags_t>(c);
	auto fdnum = args.first();
	auto sdflags = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_SHUTDOWN);
	if(res != 0) {
		return res;
	}

	mapping->fd->sock_shutdown(sdflags);
	return mapping->fd->error;
}
