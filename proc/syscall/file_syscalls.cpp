#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_file_advise(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_allocate(syscall_context &)
{
	return ENOSYS;
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

	mapping->fd->file_create(path, pathlen, type);
	return mapping->fd->error;
}

cloudabi_errno_t cloudos::syscall_file_link(syscall_context &)
{
	return ENOSYS;
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

	// TODO: take lookup flags into account, args->dirfd.flags

	// check if fd can be created with such rights
	auto fds = args.fifth();
	if((mapping->rights_inheriting & fds->fs_rights_base) != fds->fs_rights_base
	|| (mapping->rights_inheriting & fds->fs_rights_inheriting) != fds->fs_rights_inheriting) {
		get_vga_stream() << "userspace wants too many permissions\n";
		return ENOTCAPABLE;
	}

	auto path = args.second();
	auto pathlen = args.third();
	auto oflags = args.fourth();
	auto new_fd = mapping->fd->openat(path, pathlen, oflags, fds);
	if(!new_fd || mapping->fd->error != 0) {
		get_vga_stream() << "failed to openat()\n";
		if(mapping->fd->error == 0) {
			mapping->fd->error = EIO;
		}
		return mapping->fd->error;
	}

	// Depending on filetype, drop inheriting rights if they don't make sense
	auto inheriting = fds->fs_rights_inheriting;
	if(new_fd->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		inheriting = 0;
	}

	c.result = c.process()->add_fd(new_fd, fds->fs_rights_base, inheriting);
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

cloudabi_errno_t cloudos::syscall_file_readlink(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_rename(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_stat_fget(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_stat_fput(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_stat_get(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_stat_put(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_file_symlink(syscall_context &)
{
	return ENOSYS;
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

	mapping->fd->file_unlink(path, pathlen, flags);
	return mapping->fd->error;
}

