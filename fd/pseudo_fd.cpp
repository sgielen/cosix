#include "pseudo_fd.hpp"
#include <fd/scheduler.hpp>
#include <memory/allocator.hpp>

using namespace cloudos;

static void maybe_deallocate(Blk b) {
	if(b.size > 0) {
		deallocate(b);
	}
}

pseudo_fd::pseudo_fd(pseudofd_t id, shared_ptr<fd_t> r, cloudabi_filetype_t t, const char *n)
: seekable_fd_t(t, n)
, pseudo_id(id)
, reverse_fd(r)
{
}

Blk pseudo_fd::send_request(reverse_request_t *request, const char *buffer, reverse_response_t *response) {
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

	size_t received = 0;
	Blk recv_buf;

	char *msg = reinterpret_cast<char*>(request);
	assert(reverse_fd->type == CLOUDABI_FILETYPE_SOCKET_STREAM);
	if(reverse_fd->write(msg, sizeof(reverse_request_t)) != sizeof(reverse_request_t) || reverse_fd->error != 0) {
		goto error;
	}
	if(request->send_length > 0) {
		if(reverse_fd->write(buffer, request->send_length) != request->send_length || reverse_fd->error != 0) {
			goto error;
		}
	}

	msg = reinterpret_cast<char*>(response);
	while(received < sizeof(reverse_response_t)) {
		size_t remaining = sizeof(reverse_response_t) - received;
		received += reverse_fd->read(msg + received, remaining);
		if(reverse_fd->error != 0) {
			goto error;
		}
	}
	assert(received == sizeof(reverse_response_t));
	if(response->send_length > 0) {
		received = 0;
		recv_buf = allocate(response->send_length);
		msg = reinterpret_cast<char*>(recv_buf.ptr);
		while(received < response->send_length) {
			size_t remaining = response->send_length - received;
			received += reverse_fd->read(reinterpret_cast<char*>(recv_buf.ptr) + received, remaining);
			if(reverse_fd->error != 0) {
				goto error;
			}
		}
		assert(received == response->send_length);
	}
	reverse_fd->refcount = 1;
	return recv_buf;

error:
	get_vga_stream() << "Failed to send pseudo RPC or read response: " << reverse_fd->error << "\n";
	response->result = -reverse_fd->error;
	response->flags = 0;
	response->send_length = 0;
	maybe_deallocate(recv_buf);
	return {};
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
		return false;
	}
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::lookup;
	request.inode = 0;
	request.flags = oflags;
	request.send_length = length;
	request.recv_length = 0;
	maybe_deallocate(send_request(&request, path, response));
	return true;
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
	request.recv_length = count;
	reverse_response_t response;
	Blk buf = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		error = -response.result;
		maybe_deallocate(buf);
		return 0;
	}

	error = 0;
	if(response.send_length > count) {
		get_vga_stream() << "pseudo-fd filesystem returned more data than requested, dropping";
		response.send_length = count;
	}

	memcpy(dest, buf.ptr, response.send_length);
	maybe_deallocate(buf);
	pos += response.send_length;
	return response.send_length;
}

size_t pseudo_fd::write(const char *str, size_t size)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::pwrite;
	request.inode = 0;
	request.flags = 0;
	request.offset = pos;
	request.send_length = size;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, str, &response));
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
	pos += size;
	return size;
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
		request.send_length = pathlen;

		maybe_deallocate(send_request(&request, path, &response));
		if(response.result < 0) {
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

	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
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
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return 0;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::create;
	request.inode = 0;
	request.flags = type;
	request.send_length = pathlen;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, path, &response));
	if(response.result < 0) {
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
	request.send_length = pathlen;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, path, &response));
	if(response.result < 0) {
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
	request.send_length = pathlen;

	reverse_response_t response;
	Blk b = send_request(&request, path, &response);
	if(response.result < 0) {
		maybe_deallocate(b);
		error = -response.result;
	} else {
		if(b.size < sizeof(cloudabi_filestat_t)) {
			error = EIO;
			maybe_deallocate(b);
			return;
		}
		memcpy(buf, b.ptr, sizeof(cloudabi_filestat_t));
		maybe_deallocate(b);
		if(buf->st_dev != device) {
			get_vga_stream() << "Pseudo FD powered filesystem changed device ID's";
			error = EIO;
			return;
		}
		error = 0;
	}
}

void pseudo_fd::file_stat_fget(cloudabi_filestat_t *buf)
{
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_fget;
	request.inode = 0;
	request.flags = 0;

	reverse_response_t response;
	Blk b = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		maybe_deallocate(b);
		error = -response.result;
	} else {
		if(b.size < sizeof(cloudabi_filestat_t)) {
			error = EIO;
			maybe_deallocate(b);
			return;
		}
		memcpy(buf, b.ptr, sizeof(cloudabi_filestat_t));
		maybe_deallocate(b);
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

		reverse_response_t response;
		Blk b = send_request(&request, nullptr, &response);
		if(response.result < 0) {
			error = -response.result;
			maybe_deallocate(b);
			return 0;
		} else if(response.result == 0) {
			// there were no more entries
			break;
		}
		if(b.size < sizeof(cloudabi_dirent_t)) {
			// too little data returned
			error = EIO;
			maybe_deallocate(b);
			return 0;
		}
		cookie = response.result;
		// check the entry, add it to buf
		cloudabi_dirent_t *dirent = reinterpret_cast<cloudabi_dirent_t*>(b.ptr);
		if(b.size != sizeof(cloudabi_dirent_t) + dirent->d_namlen) {
			// filesystem did not provide enough data, maybe the filename didn't fit?
			error = EIO;
			maybe_deallocate(b);
			return 0;
		}
		// append this buf to the given buffer
		size_t remaining = nbyte - written;
		size_t write = remaining < b.size ? remaining : b.size;
		memcpy(buf + written, b.ptr, write);
		written += write;
		maybe_deallocate(b);
	}
	error = 0;
	return written;
}

cloudabi_errno_t pseudo_fd::lookup_device_id() {
	if(device_id_obtained) {
		return 0;
	}

	// figure out the device ID by doing an fstat()
	// NOTE: the uniqueness of device IDs is not checked, which means that
	// the userland is responsible for keeping them unique!
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_fget;
	request.inode = 0;
	request.flags = flags;

	reverse_response_t response;
	Blk b = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		maybe_deallocate(b);
		return -response.result;
	} else {
		if(b.size < sizeof(cloudabi_filestat_t)) {
			maybe_deallocate(b);
			return EIO;
		}
		auto *stat = reinterpret_cast<cloudabi_filestat_t*>(b.ptr);
		device = stat->st_dev;
		assert(device > 0 || type == CLOUDABI_FILETYPE_SOCKET_STREAM || type == CLOUDABI_FILETYPE_SOCKET_DGRAM);
		device_id_obtained = true;
		maybe_deallocate(b);
		return 0;
	}
}

void pseudo_fd::sock_bind(cloudabi_sa_family_t, shared_ptr<fd_t>, void*, size_t)
{
	// Can only bind to UNIX addresses, this is not supported at the moment
	error = EINVAL;
}

void pseudo_fd::sock_connect(cloudabi_sa_family_t, shared_ptr<fd_t>, void*, size_t)
{
	// Can only bind to UNIX addresses, this is not supported at the moment
	error = EINVAL;
}

void pseudo_fd::sock_listen(cloudabi_backlog_t backlog)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_listen;
	request.inode = 0;
	request.flags = 0;
	request.recv_length = backlog;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
	}
	error = 0;
}

shared_ptr<fd_t> pseudo_fd::sock_accept(cloudabi_sa_family_t family, void *address, size_t *address_len)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_accept;
	request.inode = 0;
	request.flags = family;
	request.recv_length = address_len == nullptr ? 0 : *address_len;

	reverse_response_t response;
	Blk b = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		error = -response.result;
		maybe_deallocate(b);
		return nullptr;
	}
	if(address != nullptr && address_len != nullptr) {
		if(*address_len < sizeof(cloudabi_sockstat_t)) {
			*address_len = 0;
		} else {
			if(b.size != sizeof(cloudabi_sockstat_t)) {
				error = EIO;
				// TODO: close pseudo FD again
				maybe_deallocate(b);
				return nullptr;
			}
			memcpy(address, b.ptr, b.size);
			*address_len = b.size;
		}
	}

	maybe_deallocate(b);
	pseudofd_t new_pseudo_id = response.result;

	char new_name[sizeof(name)];
	strncpy(new_name, name, sizeof(new_name));
	strncat(new_name, "->accepted", sizeof(new_name) - strlen(new_name) - 1);

	// TODO: is filetype of the new socket always the same as that of the accepting socket?
	auto new_fd = make_shared<pseudo_fd>(new_pseudo_id, reverse_fd, type, new_name);
	error = 0;
	return new_fd;
}

void pseudo_fd::sock_shutdown(cloudabi_sdflags_t how)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_shutdown;
	request.inode = 0;
	request.flags = how;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
	}
	error = 0;
}

void pseudo_fd::sock_stat_get(cloudabi_sockstat_t *buf, cloudabi_ssflags_t flags)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_stat_get;
	request.inode = 0;
	request.flags = flags;
	request.recv_length = sizeof(cloudabi_sockstat_t);

	reverse_response_t response;
	Blk b = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		error = -response.result;
		maybe_deallocate(b);
		return;
	}
	if(b.size != sizeof(cloudabi_sockstat_t)) {
		error = EIO;
		return;
	}
	memcpy(buf, b.ptr, b.size);
	error = 0;
	maybe_deallocate(b);
}

void pseudo_fd::sock_recv(const cloudabi_recv_in_t *in, cloudabi_recv_out_t *out)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_recv;
	request.inode = 0;
	request.flags = in->ri_flags;
	for(size_t i = 0; i < in->ri_data_len; ++i) {
		request.recv_length += in->ri_data[i].buf_len;
	}

	reverse_response_t response;
	Blk b = send_request(&request, nullptr, &response);
	if(response.result < 0) {
		error = -response.result;
		maybe_deallocate(b);
		return;
	}
	if(b.size > request.recv_length) {
		error = EIO;
		maybe_deallocate(b);
		return;
	}
	size_t off = 0;
	for(size_t i = 0; i < in->ri_data_len; ++i) {
		size_t remaining = b.size - off;
		size_t buf_len = in->ri_data[i].buf_len;
		size_t copy = buf_len < remaining ? buf_len : remaining;
		memcpy(in->ri_data[i].buf, reinterpret_cast<uint8_t*>(b.ptr) + off, copy);
		off += copy;
	}
	memset(out, 0, sizeof(cloudabi_recv_out_t));
	out->ro_datalen = b.size;
	error = 0;
}

void pseudo_fd::sock_send(const cloudabi_send_in_t *in, cloudabi_send_out_t *out)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sock_send;
	request.inode = 0;
	request.flags = in->si_flags;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		// TODO guard against overflow
		request.send_length += in->si_data[i].buf_len;
	}
	size_t off = 0;
	Blk b = allocate(request.send_length);
	for(size_t i = 0; i < in->si_data_len; ++i) {
		memcpy(reinterpret_cast<uint8_t*>(b.ptr) + off, in->si_data[i].buf, in->si_data[i].buf_len);
		off += in->si_data[i].buf_len;
	}

	reverse_response_t response;
	maybe_deallocate(send_request(&request, reinterpret_cast<const char*>(b.ptr), &response));
	deallocate(b);
	if(response.result < 0) {
		error = -response.result;
		return;
	}
	out->so_datalen = response.recv_length;
}
