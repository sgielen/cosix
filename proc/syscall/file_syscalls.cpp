#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>
#include <fd/vfs.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_file_advise(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_filesize_t, cloudabi_filesize_t, cloudabi_advice_t>(c);
	auto fdnum = args.first();
	auto advice = args.fourth();

	if(advice < CLOUDABI_ADVICE_DONTNEED || advice > CLOUDABI_ADVICE_WILLNEED) {
		return EINVAL;
	}

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_ADVISE);
	if(res != 0) {
		return res;
	}

	// TODO: act upon advice
	return 0;
}

cloudabi_errno_t cloudos::syscall_file_allocate(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_filesize_t, cloudabi_filesize_t>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_ALLOCATE);
	if(res != 0) {
		return res;
	}

	auto offset = args.second();
	auto len = args.third();

	mapping->fd->file_allocate(offset, len);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_file_create(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const char*, size_t, cloudabi_filetype_t>(c);
	auto type = args.fourth();

	cloudabi_rights_t right_needed = 0;
	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		right_needed = CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY;
	} else {
		get_vga_stream() << "Unknown file type to create, failing\n";
		return EINVAL;
	}

	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, right_needed);
	if(res != 0) {
		return res;
	}

	auto path = args.second();
	auto pathlen = args.third();

	return file_create(mapping->fd, path, pathlen, type, nullptr);
}

cloudabi_errno_t cloudos::syscall_file_link(syscall_context &c)
{
	auto args = arguments_t<cloudabi_lookup_t, const char*, size_t, cloudabi_fd_t, const char*, size_t>(c);
	auto fd1 = args.first().fd;
	fd_mapping_t *mapping1;
	auto res = c.process()->get_fd(&mapping1, fd1, CLOUDABI_RIGHT_FILE_LINK_SOURCE);
	if(res != 0) {
		return res;
	}

	auto fd2 = args.fourth();
	fd_mapping_t *mapping2;
	res = c.process()->get_fd(&mapping2, fd2, CLOUDABI_RIGHT_FILE_LINK_TARGET);
	if(res != 0) {
		return res;
	}

	auto source = args.second();
	auto sourcelen = args.third();

	auto destination = args.fifth();
	auto destinationlen = args.sixth();

	return file_link(mapping1->fd, source, sourcelen, args.first().flags, mapping2->fd, destination, destinationlen);
}

cloudabi_errno_t cloudos::syscall_file_open(syscall_context &c)
{
	auto args = arguments_t<cloudabi_lookup_t, const char*, size_t, cloudabi_oflags_t, const cloudabi_fdstat_t*, cloudabi_fd_t*>(c);
	auto dirfd = args.first();
	int fdnum = dirfd.fd;
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_OPEN);
	if(res != 0) {
		return res;
	}

	// check if fd can be created with such rights
	cloudabi_fdstat_t fds = *args.fifth();
	if((mapping->rights_inheriting & fds.fs_rights_base) != fds.fs_rights_base
	|| (mapping->rights_inheriting & fds.fs_rights_inheriting) != fds.fs_rights_inheriting) {
		get_vga_stream() << "userspace wants too many permissions\n";
		return ENOTCAPABLE;
	}

	auto lookupflags = dirfd.flags;
	auto path = args.second();
	auto pathlen = args.third();
	auto oflags = args.fourth();

	shared_ptr<fd_t> result;
	auto error = openat(mapping->fd, path, pathlen, lookupflags, oflags, &fds, result);
	if (error != 0) {
		return error;
	}

	c.result = c.process()->add_fd(result, fds.fs_rights_base, fds.fs_rights_inheriting);
	return 0;
}

cloudabi_errno_t cloudos::syscall_file_readdir(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, char*, size_t, cloudabi_dircookie_t, size_t*>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_READDIR);
	if(res != 0) {
		return res;
	}

	auto buf = args.second();
	auto len = args.third();
	auto cookie = args.fourth();
	c.result = mapping->fd->readdir(buf, len, cookie);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_file_readlink(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const char*, size_t, char*, size_t, size_t*>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_READLINK);
	if(res != 0) {
		return res;
	}

	auto *path = args.second();
	auto path_len = args.third();
	auto *buf = args.fourth();
	auto buf_len = args.fifth();

	res = file_readlink(mapping->fd, path, path_len, buf, &buf_len);
	if (res == 0) {
		c.result = buf_len;
	}
	return res;
}

cloudabi_errno_t cloudos::syscall_file_rename(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const char*, size_t, cloudabi_fd_t, const char*, size_t>(c);
	auto fd1 = args.first();
	fd_mapping_t *mapping1;
	auto res = c.process()->get_fd(&mapping1, fd1, CLOUDABI_RIGHT_FILE_RENAME_SOURCE);
	if(res != 0) {
		get_vga_stream() << "rename source failed\n";
		return res;
	}

	auto fd2 = args.fourth();
	fd_mapping_t *mapping2;
	res = c.process()->get_fd(&mapping2, fd2, CLOUDABI_RIGHT_FILE_RENAME_TARGET);
	if(res != 0) {
		get_vga_stream() << "rename target failed\n";
		return res;
	}

	auto source = args.second();
	auto sourcelen = args.third();

	auto destination = args.fifth();
	auto destinationlen = args.sixth();

	return file_rename(mapping1->fd, source, sourcelen, mapping2->fd, destination, destinationlen);
}

cloudabi_errno_t cloudos::syscall_file_stat_fget(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, cloudabi_filestat_t*>(c);
	auto fdnum = args.first();
	auto statbuf = args.second();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_STAT_FGET);
	if(res != 0) {
		return res;
	}

	mapping->fd->file_stat_fget(statbuf);
	if(mapping->fd->error == 0) {
		assert(statbuf->st_dev == mapping->fd->device);
	}
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_file_stat_fput(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, const cloudabi_filestat_t*, cloudabi_fsflags_t>(c);
	auto fdnum = args.first();
	auto statbuf = args.second();
	auto flags = args.third();

	cloudabi_fsflags_t flags_counted = 0;
	cloudabi_rights_t rights_needed = 0;

	if(flags & CLOUDABI_FILESTAT_ATIM || flags & CLOUDABI_FILESTAT_ATIM_NOW
	|| flags & CLOUDABI_FILESTAT_MTIM || flags & CLOUDABI_FILESTAT_MTIM_NOW) {
		flags_counted |= flags & (CLOUDABI_FILESTAT_ATIM | CLOUDABI_FILESTAT_ATIM_NOW | CLOUDABI_FILESTAT_MTIM | CLOUDABI_FILESTAT_MTIM_NOW);
		rights_needed |= CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES;
	}

	if(flags & CLOUDABI_FILESTAT_SIZE) {
		flags_counted |= CLOUDABI_FILESTAT_SIZE;
		rights_needed |= CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE;
	}

	if(flags != flags_counted) {
		// unknown flags given
		return EINVAL;
	}

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, rights_needed);
	if(res != 0) {
		return res;
	}

	mapping->fd->file_stat_fput(statbuf, flags);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_file_stat_get(syscall_context &c)
{
	auto args = arguments_t<cloudabi_lookup_t, const char*, size_t, cloudabi_filestat_t*>(c);
	auto dirfd = args.first();
	auto path = args.second();
	auto pathlen = args.third();
	auto statbuf = args.fourth();

	int fdnum = dirfd.fd;
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_STAT_GET);
	if(res != 0) {
		return res;
	}

	cloudabi_fdstat_t fds;
	return file_stat_get(mapping->fd, path, pathlen, dirfd.flags, &fds, statbuf);
}

cloudabi_errno_t cloudos::syscall_file_stat_put(syscall_context &c)
{
	auto args = arguments_t<cloudabi_lookup_t, const char*, size_t, const cloudabi_filestat_t*, cloudabi_fsflags_t>(c);
	auto dirfd = args.first();
	auto path = args.second();
	auto pathlen = args.third();
	auto statbuf = args.fourth();
	auto flags = args.fifth();

	cloudabi_fsflags_t flags_counted = 0;
	cloudabi_rights_t rights_needed = 0;

	if(flags & CLOUDABI_FILESTAT_ATIM || flags & CLOUDABI_FILESTAT_ATIM_NOW
	|| flags & CLOUDABI_FILESTAT_MTIM || flags & CLOUDABI_FILESTAT_MTIM_NOW) {
		flags_counted |= flags & (CLOUDABI_FILESTAT_ATIM | CLOUDABI_FILESTAT_ATIM_NOW | CLOUDABI_FILESTAT_MTIM | CLOUDABI_FILESTAT_MTIM_NOW);
		rights_needed |= CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES;
	}

	if(flags & CLOUDABI_FILESTAT_SIZE) {
		flags_counted |= CLOUDABI_FILESTAT_SIZE;
		rights_needed |= CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE;
	}

	if(flags != flags_counted) {
		// unknown flags given
		return EINVAL;
	}

	int fdnum = dirfd.fd;
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, rights_needed);
	if(res != 0) {
		return res;
	}

	cloudabi_fdstat_t fds;
	return file_stat_put(mapping->fd, path, pathlen, dirfd.flags, &fds, statbuf, flags);
}

cloudabi_errno_t cloudos::syscall_file_symlink(syscall_context &c)
{
	auto args = arguments_t<const char*, size_t, cloudabi_fd_t, const char*, size_t>(c);
	auto fdnum = args.third();

	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_SYMLINK);
	if(res != 0) {
		return res;
	}

	auto *target = args.first();
	auto targetlen = args.second();
	auto *path = args.fourth();
	auto pathlen = args.fifth();

	return file_symlink(mapping->fd, path, pathlen, target, targetlen);
}

cloudabi_errno_t cloudos::syscall_file_unlink(syscall_context &c)
{
	auto args = arguments_t<cloudabi_fd_t, char*, size_t, cloudabi_ulflags_t>(c);
	auto fdnum = args.first();
	fd_mapping_t *mapping;
	auto res = c.process()->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_UNLINK);
	if(res != 0) {
		return res;
	}

	auto path = args.second();
	auto pathlen = args.third();
	auto flags = args.fourth();

	return file_unlink(mapping->fd, path, pathlen, flags);
}
