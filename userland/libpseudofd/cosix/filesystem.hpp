#pragma once

#include <stdexcept>
#include <cloudabi_types.h>
#include "../../../fd/reverse_proto.hpp"

namespace cosix {

using reverse_proto::pseudofd_t;
using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;

struct filesystem;

void handle_request(reverse_request_t *request, reverse_response_t *response, filesystem *fs);
void handle_requests(int reversefd, filesystem *fs);

struct filesystem_error : public std::runtime_error {
	filesystem_error(cloudabi_errno_t e);

	cloudabi_errno_t error;
};

struct file_entry {
	virtual ~file_entry();

	file_entry() = default;
	file_entry(file_entry const&) = default;
	file_entry(file_entry&&) = default;
	file_entry &operator=(file_entry const&) = default;
	file_entry &operator=(file_entry&&) = default;

	cloudabi_device_t device = 0;
	cloudabi_inode_t inode = 0;
	cloudabi_filetype_t type = 0;
};

/** An abstract filesystem implementation.
 */
struct filesystem {
	filesystem(cloudabi_device_t device_id);
	virtual ~filesystem();

	virtual file_entry lookup(pseudofd_t pseudo, const char *path, size_t len, cloudabi_lookupflags_t lookupflags) = 0;

	virtual pseudofd_t open(cloudabi_inode_t inode, int flags);
	virtual void unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags);
	virtual cloudabi_inode_t create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type);
	virtual void close(pseudofd_t pseudo);
	virtual size_t pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested);
	virtual void pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length);
	virtual size_t readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie);
	virtual void stat_get(pseudofd_t pseudo, cloudabi_lookupflags_t flags, char *path, size_t len, cloudabi_filestat_t *statbuf);

	inline cloudabi_device_t get_device() {
		return device;
	}

private:
	cloudabi_device_t device;
};

}
