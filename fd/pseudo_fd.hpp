#pragma once

#include "fd.hpp"
#include "reverse_proto.hpp"

namespace cloudos {

using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;
using reverse_proto::pseudofd_t;

/** A pseudo-fd. This is a file descriptor where all calls on it are converted
 * to RPCs. These RPCs are sent to a given file descriptor, called the "reverse
 * fd". This is usually a socket with the other end given to a process, so that
 * the process can handle and respond to all calls on its pseudo fd's. It is
 * given to the constructor of the pseudo_fd. Only one request can be
 * outstanding on a reverse fd.
 *
 * The other side of the reverse FD will create a new pseudo FDs in the open
 * call.
 */
struct pseudo_fd : public seekable_fd_t {
	pseudo_fd(pseudofd_t id, shared_ptr<fd_t> reverse_fd, cloudabi_filetype_t t, const char *n);

	/* For memory, pipes and files */
	size_t read(void *dest, size_t count) override;
	void putstring(const char *str, size_t count) override;

	/* For directories */
	shared_ptr<fd_t> openat(const char *path, size_t pathlen, cloudabi_oflags_t oflags, const cloudabi_fdstat_t * fdstat) override;
	size_t readdir(char *buf, size_t nbyte, cloudabi_dircookie_t cookie) override;
	void file_create(const char *path, size_t pathlen, cloudabi_filetype_t type) override;
	void file_unlink(const char *path, size_t pathlen, cloudabi_ulflags_t flags) override;

private:
	reverse_response_t *send_request(reverse_request_t *request);
	bool is_valid_path(const char *path, size_t length);
	reverse_response_t *lookup_inode(const char *path, size_t length, cloudabi_oflags_t oflags);

	pseudofd_t pseudo_id;
	shared_ptr<fd_t> reverse_fd;
};

}
