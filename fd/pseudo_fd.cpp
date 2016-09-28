#include "pseudo_fd.hpp"
#include <fd/scheduler.hpp>
#include <memory/allocator.hpp>

using namespace cloudos;

pseudo_fd::pseudo_fd(pseudofd_t id, fd_t *r, cloudabi_filetype_t t, const char *n)
: fd_t(t, n)
, pseudo_id(id)
, reverse_fd(r)
{
}

reverse_response_t *pseudo_fd::send_request(reverse_request_t *request) {
	// Lock the reverse_fd. Multiple pseudo FD's may have a reference to
	// this reverse_fd, and another one may have an outstanding request
	// already.
	// Since we have no true 'locks' yet, we are uniprocessor and no
	// kernel thread preemption, we can 'lock' an FD by setting its
	// refcount (a currently unused parameter) to 2. We yield until we
	// catch our reverse FD with a refcount of 1. This approach has many
	// problems but it's acceptable for now.
	while(reverse_fd->refcount == 2) {
		get_scheduler()->thread_yield();
	}
	reverse_fd->refcount = 2;

	char *msg = reinterpret_cast<char*>(request);
	reverse_fd->putstring(msg, sizeof(reverse_request_t));

	reverse_response_t *resp = get_allocator()->allocate<reverse_response_t>();
	msg = reinterpret_cast<char*>(resp);
	size_t received = 0;
	while(received < sizeof(reverse_response_t)) {
		// TODO: error checking on the reverse_fd, in case it closed
		size_t remaining = sizeof(reverse_response_t) - received;
		received += reverse_fd->read(0, resp + received, remaining);
	}
	reverse_fd->refcount = 1;
	return resp;
}

bool pseudo_fd::is_valid_path(const char *path, size_t length)
{
	for(size_t i = 0; i < length; ++i) {
		if(path[i] < 0x20 || path[i] == 0x7f) {
			// control character
			return false;
		}
	}

	return true;
}

reverse_response_t *pseudo_fd::lookup_inode(const char *path, size_t length, cloudabi_oflags_t oflags)
{
	if(!is_valid_path(path, length)) {
		return nullptr;
	}
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::lookup;
	request.inode = 0;
	request.flags = oflags;
	if(length > sizeof(request.buffer)) {
		length = sizeof(request.buffer);
	}
	request.length = length;
	memcpy(request.buffer, path, length);
	return send_request(&request);
}

size_t pseudo_fd::read(size_t offset, void *dest, size_t count)
{
	if(count > UINT8_MAX) {
		count = UINT8_MAX;
	}
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::pread;
	request.inode = 0;
	request.flags = 0;
	request.offset = offset;
	request.length = count;
	reverse_response_t *response = send_request(&request);
	if(!response || response->result < 0) {
		// TODO error handling
		error = error_t::invalid_argument;
		return 0;
	}

	error = error_t::no_error;
	memcpy(dest, response->buffer, response->length < count ? response->length : count);
	return response->length;
}

error_t pseudo_fd::putstring(const char *str, size_t remaining)
{
	size_t copied = 0;
	while(remaining > 0) {
		size_t count = remaining;
		if(count > UINT8_MAX) {
			count = UINT8_MAX;
		}
		remaining -= count;

		reverse_request_t request;
		request.pseudofd = pseudo_id;
		request.op = reverse_request_t::operation::pwrite;
		request.inode = 0;
		request.flags = 0;
		request.offset = 0; /* TODO */
		request.length = count;
		memcpy(request.buffer, str + copied, count);
		copied += count;
		reverse_response_t *response = send_request(&request);
		if(!response || response->result < 0) {
			// TODO error handling
			error = error_t::invalid_argument;
			return error;
		}
	}
	error = error_t::no_error;
	return error;
}

fd_t *pseudo_fd::openat(const char *path, size_t pathlen, cloudabi_oflags_t oflags, const cloudabi_fdstat_t * fdstat)
{
	// TODO HACK because of exception breakage in tmpfs
	// if O_CREAT is given, assume the file doesn't exist, create it
	int64_t inode;
	int filetype;
	if(oflags & CLOUDABI_O_CREAT) {
		filetype = CLOUDABI_FILETYPE_REGULAR_FILE;
		reverse_request_t request;
		request.pseudofd = pseudo_id;
		request.op = reverse_request_t::operation::create;
		request.inode = 0;
		request.flags = filetype;
		request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
		memcpy(request.buffer, path, request.length);

		reverse_response_t *response = send_request(&request);
		inode = response->result;
	} else {
		reverse_response_t *response = lookup_inode(path, pathlen, oflags);
		if(!response /* invalid path */) {
			error = error_t::invalid_argument;
			return nullptr;
		} else if(response->result < 0) {
			// TODO: errno to error to errno
			error = error_t::invalid_argument;
			return nullptr;
		}

		inode = response->result;
		filetype = response->flags;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::open;
	request.inode = inode;
	request.flags = oflags;
	request.length = 0;
	reverse_response_t *response = send_request(&request);

	if(!response) {
		// TODO: this can't currently happen
		error = error_t::invalid_argument;
		return nullptr;
	} else if(response->result < 0) {
		// TODO: errno to error to errno
		error = error_t::invalid_argument;
		return nullptr;
	}

	pseudofd_t new_pseudo_id = response->result;

	pseudo_fd *new_fd = get_allocator()->allocate<pseudo_fd>();
	char new_name[sizeof(name)];
	strncpy(new_name, name, sizeof(new_name));
	strncat(new_name, "->", sizeof(new_name) - strlen(new_name) - 1);
	size_t copy = sizeof(new_name) - strlen(new_name) - 1;
	if(copy > pathlen) copy = pathlen;
	strncat(new_name, path, copy);
	new (new_fd) pseudo_fd(new_pseudo_id, reverse_fd, filetype, new_name);
	// TODO: check if the rights are actually obtainable before opening the file;
	// ignore those that don't apply to this filetype, return ENOTCAPABLE if not
	new_fd->flags = fdstat->fs_flags;

	return new_fd;
}

void pseudo_fd::file_create(const char *path, size_t pathlen, cloudabi_filetype_t type)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::create;
	request.inode = 0;
	request.flags = type;
	request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
	memcpy(request.buffer, path, request.length);

	reverse_response_t *response = send_request(&request);
	if(!response /* invalid path */) {
		error = error_t::invalid_argument;
	} else if(response->result < 0) {
		// TODO: errno to error to errno
		error = error_t::invalid_argument;
	} else {
		error = error_t::no_error;
	}
}
