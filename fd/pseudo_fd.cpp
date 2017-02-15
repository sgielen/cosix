#include "pseudo_fd.hpp"
#include <fd/scheduler.hpp>
#include <memory/allocator.hpp>

using namespace cloudos;

pseudo_fd::pseudo_fd(pseudofd_t id, shared_ptr<fd_t> r, cloudabi_filetype_t t, const char *n)
: seekable_fd_t(t, n)
, pseudo_id(id)
, reverse_fd(r)
{
}

bool pseudo_fd::send_request(reverse_request_t *request, reverse_response_t *response) {
	response->result = -EIO;
	response->flags = 0;
	response->length = 0;

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

	msg = reinterpret_cast<char*>(response);
	size_t received = 0;
	while(received < sizeof(reverse_response_t)) {
		// TODO: error checking on the reverse_fd, in case it closed
		size_t remaining = sizeof(reverse_response_t) - received;
		received += reverse_fd->read(msg + received, remaining);
	}
	reverse_fd->refcount = 1;
	return true;
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

bool pseudo_fd::lookup_inode(const char *path, size_t length, cloudabi_oflags_t oflags, reverse_response_t *response)
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
	return send_request(&request, response);
}

size_t pseudo_fd::read(void *dest, size_t count)
{
	if(count > UINT8_MAX) {
		count = UINT8_MAX;
	}
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::pread;
	request.inode = 0;
	request.flags = 0;
	request.offset = pos;
	request.length = count;
	reverse_response_t response;
	if(!send_request(&request, &response) || response.result < 0) {
		error = -response.result;
		return 0;
	}

	error = 0;
	if(response.length < count) {
		count = response.length;
	}
	else if(response.length > count) {
		get_vga_stream() << "pseudo-fd filesystem returned more data than requested, dropping";
	}

	memcpy(dest, response.buffer, count);
	pos += count;
	return count;
}

void pseudo_fd::putstring(const char *str, size_t remaining)
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
		reverse_response_t response;
		if(!send_request(&request, &response) || response.result < 0) {
			error = -response.result;
			return;
		}
	}
	error = 0;
}

shared_ptr<fd_t> pseudo_fd::openat(const char *path, size_t pathlen, cloudabi_oflags_t oflags, const cloudabi_fdstat_t * fdstat)
{
	int64_t inode;
	int filetype;
	reverse_response_t response;
	if(!lookup_inode(path, pathlen, oflags, &response)) {
		error = -response.result;
		return nullptr;
	} else if(response.result == -ENOENT && (oflags & CLOUDABI_O_CREAT)) {
		// The file doesn't exist and should be created.
		filetype = CLOUDABI_FILETYPE_REGULAR_FILE;
		reverse_request_t request;
		request.pseudofd = pseudo_id;
		request.op = reverse_request_t::operation::create;
		request.inode = 0;
		request.flags = filetype;
		request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
		memcpy(request.buffer, path, request.length);

		if(!send_request(&request, &response) || response.result < 0) {
			error = -response.result;
			return nullptr;
		}

		inode = response.result;
	} else if(response.result < 0) {
		error = -response.result;
		return nullptr;
	} else {
		inode = response.result;
		filetype = response.flags;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::open;
	request.inode = inode;
	request.flags = oflags;
	request.length = 0;

	if(!send_request(&request, &response) || response.result < 0) {
		error = -response.result;
		return nullptr;
	}

	pseudofd_t new_pseudo_id = response.result;

	char new_name[sizeof(name)];
	strncpy(new_name, name, sizeof(new_name));
	strncat(new_name, "->", sizeof(new_name) - strlen(new_name) - 1);
	size_t copy = sizeof(new_name) - strlen(new_name) - 1;
	if(copy > pathlen) copy = pathlen;
	strncat(new_name, path, copy);

	auto new_fd = make_shared<pseudo_fd>(new_pseudo_id, reverse_fd, filetype, new_name);
	// TODO: check if the rights are actually obtainable before opening the file;
	// ignore those that don't apply to this filetype, return ENOTCAPABLE if not
	new_fd->flags = fdstat->fs_flags;
	error = 0;

	return new_fd;
}

cloudabi_inode_t pseudo_fd::file_create(const char *path, size_t pathlen, cloudabi_filetype_t type)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::create;
	request.inode = 0;
	request.flags = type;
	request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
	memcpy(request.buffer, path, request.length);

	reverse_response_t response;
	if(!send_request(&request, &response) || response.result < 0) {
		error = -response.result;
		return 0;
	} else {
		error = 0;
		return response.result;
	}
}

void pseudo_fd::file_unlink(const char *path, size_t pathlen, cloudabi_ulflags_t flags)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::unlink;
	request.inode = 0;
	request.flags = flags;
	request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
	memcpy(request.buffer, path, request.length);

	reverse_response_t response;
	if(!send_request(&request, &response) || response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::file_stat_get(cloudabi_lookupflags_t flags, const char *path, size_t pathlen, cloudabi_filestat_t *buf)
{
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_get;
	request.inode = 0;
	request.flags = flags;
	request.length = pathlen < sizeof(request.buffer) ? pathlen : sizeof(request.buffer);
	memcpy(request.buffer, path, request.length);

	reverse_response_t response;
	if(!send_request(&request, &response) || response.result < 0) {
		error = -response.result;
	} else {
		if(response.length < sizeof(cloudabi_filestat_t)) {
			error = EIO;
			return;
		}
		memcpy(buf, response.buffer, sizeof(cloudabi_filestat_t));
		if(buf->st_dev != device) {
			get_vga_stream() << "Pseudo FD powered filesystem changed device ID's";
			error = EIO;
			return;
		}
		error = 0;
	}
}

size_t pseudo_fd::readdir(char *buf, size_t nbyte, cloudabi_dircookie_t cookie)
{
	// TODO: the RPC protocol allows requesting a single readdir buffer. We
	// will keep requesting such buffers until our own buffer is full, or
	// until there are no left.
	size_t written = 0;
	while(written < nbyte) {
		reverse_request_t request;
		request.pseudofd = pseudo_id;
		request.op = reverse_request_t::operation::readdir;
		request.flags = cookie;
		request.length = 0;
		reverse_response_t response;
		if(!send_request(&request, &response) || response.result < 0) {
			error = -response.result;
			return 0;
		} else if(response.result == 0) {
			// there were no more entries
			break;
		}
		cookie = response.result;
		// check the entry, add it to buf
		cloudabi_dirent_t *dirent = reinterpret_cast<cloudabi_dirent_t*>(response.buffer);
		if(response.length != sizeof(cloudabi_dirent_t) + dirent->d_namlen) {
			// filesystem did not provide enough data, maybe the filename didn't fit?
			error = EIO;
			return 0;
		}
		// append this buf to the given buffer
		size_t remaining = nbyte - written;
		size_t write = remaining < response.length ? remaining : response.length;
		memcpy(buf + written, response.buffer, write);
		written += write;
	}
	error = 0;
	return written;
}

cloudabi_errno_t pseudo_fd::lookup_device_id() {
	if(device_id_obtained) {
		return 0;
	}

	// figure out the device ID by doing a stat() for .
	// NOTE: the uniqueness of device IDs is not checked, which means that
	// the userland is responsible for keeping them unique!
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_get;
	request.inode = 0;
	request.flags = flags;
	request.length = 1;
	request.buffer[0] = '.';

	reverse_response_t response;
	if(!send_request(&request, &response) || response.result < 0) {
		return -response.result;
	} else {
		if(response.length < sizeof(cloudabi_filestat_t)) {
			return EIO;
		}
		auto *stat = reinterpret_cast<cloudabi_filestat_t*>(&response.buffer[0]);
		device = stat->st_dev;
		device_id_obtained = true;
		return 0;
	}
}

