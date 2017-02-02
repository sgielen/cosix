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
#include "cosix/filesystem.hpp"

using namespace cosix;

filesystem_error::filesystem_error(cloudabi_errno_t e)
: std::runtime_error(strerror(e))
, error(e)
{}

filesystem::filesystem(cloudabi_device_t device_id)
: device(device_id)
{}

filesystem::~filesystem() {}

pseudofd_t filesystem::open(cloudabi_inode_t, int) {
	throw filesystem_error(ENOSYS);
}

void filesystem::unlink(pseudofd_t, const char*, size_t, cloudabi_ulflags_t) {
	throw filesystem_error(ENOSYS);
}

cloudabi_inode_t filesystem::create(pseudofd_t, const char*, size_t, cloudabi_filetype_t) {
	throw filesystem_error(ENOSYS);
}

void filesystem::close(pseudofd_t) {
	throw filesystem_error(ENOSYS);
}

size_t filesystem::pread(pseudofd_t, off_t, char*, size_t) {
	throw filesystem_error(ENOSYS);
}

void filesystem::pwrite(pseudofd_t, off_t, const char*, size_t) {
	throw filesystem_error(ENOSYS);
}

size_t filesystem::readdir(pseudofd_t, char*, size_t, cloudabi_dircookie_t&) {
	throw filesystem_error(ENOSYS);
}
