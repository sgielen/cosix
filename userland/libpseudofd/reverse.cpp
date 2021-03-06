#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "cosix/reverse.hpp"

using namespace cosix;

cloudabi_system_error::cloudabi_system_error(cloudabi_errno_t e)
: std::runtime_error(strerror(e))
, error(e)
{}

cosix::file_entry::~file_entry() {}

reverse_handler::reverse_handler()
: device(0)
{}

reverse_handler::~reverse_handler() {}

file_entry reverse_handler::lookup(pseudofd_t, const char*, size_t, cloudabi_oflags_t, cloudabi_filestat_t*) {
	throw cloudabi_system_error(EINVAL);
}

std::pair<pseudofd_t, cloudabi_filetype_t> reverse_handler::open(cloudabi_inode_t) {
	throw cloudabi_system_error(EINVAL);
}

size_t reverse_handler::readlink(pseudofd_t, const char*, size_t, char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::rename(pseudofd_t, const char*, size_t, pseudofd_t, const char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::link(pseudofd_t, const char*, size_t, cloudabi_lookupflags_t, pseudofd_t, const char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::allocate(pseudofd_t, off_t, off_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::symlink(pseudofd_t, const char*, size_t, const char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::unlink(pseudofd_t, const char*, size_t, cloudabi_ulflags_t) {
	throw cloudabi_system_error(EINVAL);
}

cloudabi_inode_t reverse_handler::create(pseudofd_t, const char*, size_t, cloudabi_filetype_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::close(pseudofd_t) {
	throw cloudabi_system_error(EINVAL);
}

bool reverse_handler::is_readable(pseudofd_t, size_t&, bool&) {
	throw cloudabi_system_error(EINVAL);
}

size_t reverse_handler::pread(pseudofd_t, off_t, char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::pwrite(pseudofd_t, off_t, const char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

size_t reverse_handler::sock_recv(pseudofd_t, char*, size_t) {
	throw cloudabi_system_error(ENOTSOCK);
}

void reverse_handler::sock_send(pseudofd_t, const char*, size_t) {
	throw cloudabi_system_error(ENOTSOCK);
}

size_t reverse_handler::readdir(pseudofd_t, char*, size_t, cloudabi_dircookie_t&) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::stat_fget(pseudofd_t, cloudabi_filestat_t*) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::stat_fput(pseudofd_t, const cloudabi_filestat_t*, cloudabi_fsflags_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::stat_put(pseudofd_t, cloudabi_lookupflags_t, const char*, size_t, const cloudabi_filestat_t*, cloudabi_fsflags_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::datasync(pseudofd_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::sync(pseudofd_t) {
	throw cloudabi_system_error(EINVAL);
}
