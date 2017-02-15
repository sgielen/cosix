#include <proc/syscalls.hpp>
#include <fd/process_fd.hpp>
#include <global.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_sock_accept(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_sockstat_t *, cloudabi_fd_t *>(c);
	auto fdnum = args.first();
	auto sockstat = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_ACCEPT);
	if(res != 0) {
		return res;
	}

	auto conn = mapping->fd->sock_accept(CLOUDABI_AF_UNIX, nullptr, nullptr);
	if(mapping->fd->error != 0) {
		return mapping->fd->error;
	}
	
	if(sockstat != nullptr) {
		conn->sock_stat_get(sockstat, 0);
		if(conn->error != 0) {
			return conn->error;
		}
	}

	c.result = c.process()->add_fd(conn, mapping->rights_inheriting, 0);
	return 0;
}

cloudabi_errno_t cloudos::syscall_sock_bind(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_fd_t, void *, size_t>(c);
	auto fdnum = args.first();
	auto dirfdnum = args.second();
	auto dir = args.third();
	auto dirlen = args.fourth();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_BIND_SOCKET);
	if(res != 0) {
		return res;
	}
	auto sock = mapping->fd;
	res = c.process()->get_fd(&mapping, dirfdnum, CLOUDABI_RIGHT_SOCK_BIND_DIRECTORY);
	if(res != 0) {
		return res;
	}
	auto dirfd = mapping->fd;

	sock->sock_bind(CLOUDABI_AF_UNIX, dirfd, dir, dirlen);
	return sock->error;
}

cloudabi_errno_t cloudos::syscall_sock_connect(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_fd_t, void*, size_t>(c);
	auto fdnum = args.first();
	auto dirfdnum = args.second();
	auto dir = args.third();
	auto dirlen = args.fourth();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_CONNECT_SOCKET);
	if(res != 0) {
		return res;
	}
	auto sock = mapping->fd;
	res = c.process()->get_fd(&mapping, dirfdnum, CLOUDABI_RIGHT_SOCK_CONNECT_DIRECTORY);
	if(res != 0) {
		return res;
	}
	auto dirfd = mapping->fd;

	sock->sock_connect(CLOUDABI_AF_UNIX, dirfd, dir, dirlen);
	return sock->error;
}

cloudabi_errno_t cloudos::syscall_sock_listen(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_backlog_t>(c);
	auto fdnum = args.first();
	auto backlog = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_LISTEN);
	if(res != 0) {
		return res;
	}

	mapping->fd->sock_listen(backlog);
	return mapping->fd->error;
}

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

cloudabi_errno_t cloudos::syscall_sock_stat_get(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_sockstat_t*, cloudabi_ssflags_t>(c);
	auto fdnum = args.first();
	auto sockstat = args.second();
	auto ssflags = args.third();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_SOCK_STAT_GET);
	if(res != 0) {
		return res;
	}

	mapping->fd->sock_stat_get(sockstat, ssflags);
	return mapping->fd->error;
}

