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

file_entry reverse_handler::lookup(pseudofd_t, const char*, size_t, cloudabi_lookupflags_t) {
	throw cloudabi_system_error(EINVAL);
}

pseudofd_t reverse_handler::open(cloudabi_inode_t, int) {
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

bool reverse_handler::is_readable(pseudofd_t) {
	throw cloudabi_system_error(EINVAL);
}

size_t reverse_handler::pread(pseudofd_t, off_t, char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::pwrite(pseudofd_t, off_t, const char*, size_t) {
	throw cloudabi_system_error(EINVAL);
}

size_t reverse_handler::readdir(pseudofd_t, char*, size_t, cloudabi_dircookie_t&) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::stat_get(pseudofd_t, cloudabi_lookupflags_t, char*, size_t, cloudabi_filestat_t*) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::stat_fget(pseudofd_t, cloudabi_filestat_t*) {
	throw cloudabi_system_error(EINVAL);
}

pseudofd_t reverse_handler::sock_accept(pseudofd_t, cloudabi_sockstat_t*) {
	throw cloudabi_system_error(EINVAL);
}

void reverse_handler::sock_stat_get(pseudofd_t, cloudabi_sockstat_t*) {
	throw cloudabi_system_error(EINVAL);
}
