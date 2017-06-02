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
	cloudabi_inode_t file_create(const char *path, size_t pathlen, cloudabi_filetype_t type) override;
	void file_unlink(const char *path, size_t pathlen, cloudabi_ulflags_t flags) override;
	void file_stat_get(cloudabi_lookupflags_t flags, const char *path, size_t pathlen, cloudabi_filestat_t *buf) override;

	/* For sockets */
	void sock_bind(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void * address, size_t address_len) override;
	void sock_connect(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void * address, size_t address_len) override;
	void sock_listen(cloudabi_backlog_t backlog) override;
	shared_ptr<fd_t> sock_accept(cloudabi_sa_family_t family, void * address, size_t* address_len) override;
	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t* out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t* out) override;

private:
	cloudabi_errno_t lookup_device_id();
	Blk send_request(reverse_request_t *request, const char *buf, reverse_response_t *response);
	bool is_valid_path(const char *path, size_t length);
	bool lookup_inode(const char *path, size_t length, cloudabi_oflags_t oflags, reverse_response_t *response);

	pseudofd_t pseudo_id;
	shared_ptr<fd_t> reverse_fd;
	bool device_id_obtained = false;
};

}
