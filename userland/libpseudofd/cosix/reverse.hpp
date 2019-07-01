#pragma once

#include <stdexcept>
#include <cloudabi_types.h>
#include <stdio.h>
#include <thread>
#include "../../../fd/reverse_proto.hpp"

namespace cosix {

using reverse_proto::pseudofd_t;
using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;

struct reverse_handler;

char *handle_request(reverse_request_t *request, char *buf, reverse_response_t *response, reverse_handler *h);
// poll_timeout is an absolute cloudabi_timestamp_t; if it is reached, this
// function will return EAGAIN.
cloudabi_errno_t wait_for_request(int reversefd, cloudabi_timestamp_t poll_timeout);
char *read_request(int reversefd, reverse_request_t *request);
void write_response(int reversefd, reverse_response_t *response, char *buf);
// these functions return EAGAIN when the timeout passed without handling any request,
// 0 if a request was successfully handled, or an error otherwise.
cloudabi_errno_t handle_request(int reversefd, reverse_handler *h, cloudabi_timestamp_t poll_timeout = 0);
cloudabi_errno_t handle_request(int reversefd, reverse_handler *h, std::mutex&, cloudabi_timestamp_t poll_timeout = 0);
void handle_requests(int reversefd, reverse_handler *h);

// notify the kernel that the pseudo FD becomes readable
// Since this writes messages to the reverse FD, this function is not threadsafe.
// It is safe to call while handling a request.
void pseudo_fd_becomes_readable(int reversefd, pseudofd_t);

// Pseudo-related calls to the kernel
// returns (reverse, pseudo)
std::pair<int, int> open_pseudo(int ifstorefd, cloudabi_filetype_t type);

struct cloudabi_system_error : public std::runtime_error {
	cloudabi_system_error(cloudabi_errno_t e);

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

/** An implementation of something that can handle reverse requests.
 */
struct reverse_handler {
	reverse_handler();
	virtual ~reverse_handler();

	virtual file_entry lookup(pseudofd_t pseudo, const char *file, size_t len, cloudabi_oflags_t oflags, cloudabi_filestat_t *statbuf);

	virtual std::pair<pseudofd_t, cloudabi_filetype_t> open(cloudabi_inode_t inode);
	virtual size_t readlink(pseudofd_t pseudo, const char *filename, size_t len, char *buf, size_t buflen);
	virtual void rename(pseudofd_t pseudo1, const char *filename1, size_t filename1len, pseudofd_t pseudo2, const char *filename2, size_t filename2len);
	virtual void symlink(pseudofd_t pseudo ,const char *target, size_t targetlen, const char *filename, size_t len);
	virtual void link(pseudofd_t pseudo1, const char *filename1, size_t filename1len, cloudabi_lookupflags_t lookupflags, pseudofd_t pseudo2, const char *filename2, size_t filename2len);
	virtual void unlink(pseudofd_t pseudo, const char *filename, size_t len, cloudabi_ulflags_t unlinkflags);
	virtual cloudabi_inode_t create(pseudofd_t pseudo, const char *filename, size_t len, cloudabi_filetype_t type);
	virtual void allocate(pseudofd_t pseudo, off_t offset, off_t length);
	virtual void close(pseudofd_t pseudo);
	virtual bool is_readable(pseudofd_t pseudo, size_t &nbytes, bool &hangup);
	virtual size_t pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested);
	virtual void pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length);
	virtual void datasync(pseudofd_t pseudo);
	virtual void sync(pseudofd_t pseudo);
	virtual size_t readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie);
	virtual size_t sock_recv(pseudofd_t pseudo, char *dest, size_t requested);
	virtual void sock_send(pseudofd_t pseudo, const char *buf, size_t length);
	virtual void stat_fget(pseudofd_t pseudo, cloudabi_filestat_t *statbuf);
	virtual void stat_fput(pseudofd_t pseudo, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags);
	virtual void stat_put(pseudofd_t pseudo, cloudabi_lookupflags_t lookupflags, const char *filename, size_t len, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags);

	cloudabi_device_t device;
};

}
