#include <proc/syscalls.hpp>
#include <fd/process_fd.hpp>
#include <fd/unixsock.hpp>
#include <global.hpp>

using namespace cloudos;

// All rights, except all rights that were removed over time.
static const cloudabi_rights_t all_rights = 0x0815ffff7ff;

cloudabi_errno_t cloudos::syscall_fd_close(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t>(c);
	auto fdnum = args.first();
	return c.process()->close_fd(fdnum);
}

cloudabi_errno_t cloudos::syscall_fd_create1(syscall_context &c)
{
	auto args = arguments_t<cloudabi_filetype_t, cloudabi_fd_t*>(c);
	auto type = args.first();

	if(type == CLOUDABI_FILETYPE_SHARED_MEMORY) {
		return ENOSYS;
	} else {
		return EINVAL;
	}
}

cloudabi_errno_t cloudos::syscall_fd_create2(syscall_context &c)
{
	auto args = arguments_t<cloudabi_filetype_t, cloudabi_fd_t*, cloudabi_fd_t*>(c);
	auto type = args.first();

	if(type == CLOUDABI_FILETYPE_SOCKET_DGRAM
	|| type == CLOUDABI_FILETYPE_SOCKET_STREAM)
	{
		auto a = make_shared<unixsock>(type, 0, "socketpair A");
		auto b = make_shared<unixsock>(type, 0, "socketpair B");
		a->socketpair(b);
		assert(a->error == 0);
		assert(b->error == 0);

		auto sock_rights = CLOUDABI_RIGHT_FD_READ
				| CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS
				| CLOUDABI_RIGHT_FD_WRITE
				| CLOUDABI_RIGHT_FILE_STAT_FGET
				| CLOUDABI_RIGHT_POLL_FD_READWRITE
				| CLOUDABI_RIGHT_SOCK_SHUTDOWN;
		cloudabi_rights_t sock_inheriting = all_rights;

		auto afd = c.process()->add_fd(a, sock_rights, sock_inheriting);
		auto bfd = c.process()->add_fd(b, sock_rights, sock_inheriting);
		c.set_results(afd, bfd);
		return 0;
	} else {
		return EINVAL;
	}
}

cloudabi_errno_t cloudos::syscall_fd_datasync(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_DATASYNC);
	if(res != 0) {
		return res;
	}

	mapping->fd->datasync();
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_fd_dup(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_fd_t*>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, 0);
	if(res != 0) {
		return res;
	}

	c.result = c.process()->add_fd(mapping->fd, mapping->rights_base, mapping->rights_inheriting);
	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_pread(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_iovec_t *, size_t, size_t, size_t *>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ | CLOUDABI_RIGHT_FD_SEEK);
	if(res != 0) {
		return res;
	}

	auto iov = args.second();
	auto iovcnt = args.third();
	auto offset = args.fourth();

	if(iovcnt == 0) {
		c.result = 0;
		return 0;
	}

	// TODO: pass iov, iovcnt to read() instead of calling read() multiple
	// times
	size_t r;
	size_t i = 0;
	size_t read = 0;
	do {
		auto buf = iov[i].buf;
		auto len = iov[i].buf_len;
		r = mapping->fd->pread(buf, len, offset + read);
		if(mapping->fd->error) {
			return mapping->fd->error;
		}
		read += r;
		i++;
	} while(r > 0 && i < iovcnt);
	c.result = read;
	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_pwrite(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_ciovec_t*, size_t, size_t, size_t*>(c);
	auto fdnum = args.first();
	auto iov = args.second();
	auto iovcnt = args.third();
	auto offset = args.fourth();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE | CLOUDABI_RIGHT_FD_SEEK);
	if(res != 0) {
		return res;
	}

	// TODO: pass iov, iovcnt to pwrite() instead of calling pwrite() multiple
	// times
	c.result = 0;
	for(size_t i = 0; i < iovcnt; ++i) {
		auto buf = iov[i].buf;
		auto len = iov[i].buf_len;
		mapping->fd->pwrite(reinterpret_cast<const char*>(buf), len, offset + c.result);
		c.result += len;
		if(mapping->fd->error) {
			return mapping->fd->error;
		}
	}
	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_read(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_iovec_t *, size_t, size_t *>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ);
	if(res != 0) {
		return res;
	}

	auto iov = args.second();
	auto iovcnt = args.third();

	if(iovcnt == 0) {
		c.result = 0;
		return 0;
	}

	// TODO: pass iov, iovcnt to read() instead of calling read() multiple
	// times
	size_t r;
	size_t i = 0;
	size_t read = 0;
	do {
		auto buf = iov[i].buf;
		auto len = iov[i].buf_len;
		r = mapping->fd->read(buf, len);
		if(mapping->fd->error) {
			return mapping->fd->error;
		}
		read += r;
		i++;
	} while(r > 0 && i < iovcnt);
	c.result = read;
	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_replace(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_fd_t>(c);
	auto fromnum = args.first();
	auto tonum = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fromnum, 0);
	if(res != 0) {
		return res;
	}

	if(fromnum == tonum) {
		return 0;
	}

	return c.process()->replace_fd(tonum, mapping->fd, mapping->rights_base, mapping->rights_inheriting);
}

cloudabi_errno_t cloudos::syscall_fd_seek(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_filedelta_t, cloudabi_whence_t, cloudabi_filesize_t*>(c);
	auto fdnum = args.first();
	auto offset = args.second();
	auto whence = args.third();

	cloudabi_rights_t rights_needed = CLOUDABI_RIGHT_FD_TELL;
	if(whence != CLOUDABI_WHENCE_CUR || offset != 0) {
		rights_needed |= CLOUDABI_RIGHT_FD_SEEK;
	}
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, rights_needed);
	if(res != 0) {
		return res;
	}

	c.result = mapping->fd->seek(offset, whence);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_fd_stat_get(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_fdstat_t*>(c);
	auto fdnum = args.first();
	auto stat = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, 0);
	if(res != 0) {
		return res;
	}

	stat->fs_filetype = mapping->fd->type;
	stat->fs_flags = mapping->fd->flags;
	stat->fs_rights_base = mapping->rights_base;
	stat->fs_rights_inheriting = mapping->rights_inheriting;
	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_stat_put(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_fdstat_t*, cloudabi_fdsflags_t>(c);
	auto fdnum = args.first();
	auto stat = args.second();
	auto flags = args.third();

	if((flags & ~(CLOUDABI_FDSTAT_FLAGS | CLOUDABI_FDSTAT_RIGHTS)) != 0) {
		return EINVAL;
	}

	cloudabi_rights_t rights_needed = 0;
	if(flags & CLOUDABI_FDSTAT_FLAGS) {
		rights_needed |= CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS;
	}

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, rights_needed);
	if(res != 0) {
		return res;
	}

	if(flags & CLOUDABI_FDSTAT_FLAGS) {
		mapping->fd->flags = stat->fs_flags;
	}
	if(flags & CLOUDABI_FDSTAT_RIGHTS) {
		mapping->rights_base = mapping->rights_base & stat->fs_rights_base;
		mapping->rights_inheriting = mapping->rights_inheriting & stat->fs_rights_inheriting;
	}

	return 0;
}

cloudabi_errno_t cloudos::syscall_fd_sync(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_SYNC);
	if(res != 0) {
		return res;
	}

	mapping->fd->sync();
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_fd_write(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_ciovec_t*, size_t, size_t*>(c);
	auto fdnum = args.first();
	auto iov = args.second();
	auto iovcnt = args.third();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE);
	if(res != 0) {
		return res;
	}

	// TODO: pass iov, iovcnt to write() instead of calling write() multiple
	// times
	c.result = 0;
	for(size_t i = 0; i < iovcnt; ++i) {
		auto buf = iov[i].buf;
		auto len = iov[i].buf_len;
		mapping->fd->write(reinterpret_cast<const char*>(buf), len);
		c.result += len;
		if(mapping->fd->error) {
			return mapping->fd->error;
		}
	}
	return 0;
}
