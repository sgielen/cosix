#include "pseudo_fd.hpp"
#include <fd/scheduler.hpp>
#include <oslibc/numeric.h>

using namespace cloudos;

static void maybe_deallocate(Blk b) {
	if(b.size > 0) {
		deallocate(b);
	}
}

static bool pseudofd_is_readable(void *r, thread_condition*, thread_condition_data **data) {
	auto *fd = reinterpret_cast<pseudo_fd*>(r);
	size_t nbytes;
	bool hangup;
	bool readable = fd->is_readable(nbytes, hangup);
	if(data) {
		auto *d = allocate<thread_condition_data_fd_readwrite>();
		d->nbytes = nbytes;
		d->flags = hangup ? CLOUDABI_EVENT_FD_READWRITE_HANGUP : 0;
		*data = d;
	}
	return readable;
}

pseudo_fd::pseudo_fd(pseudofd_t id, shared_ptr<reversefd_t> r, cloudabi_filetype_t t, cloudabi_fdflags_t f, const char *n)
: seekable_fd_t(t, f, n)
, pseudo_id(id)
, reverse_fd(r)
{
	recv_signaler.set_already_satisfied_function(pseudofd_is_readable, this);
}

pseudo_fd::~pseudo_fd()
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::close;
	request.inode = 0;
	request.flags = 0;
	request.send_length = 0;
	request.recv_length = 0;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
}

Blk pseudo_fd::send_request(reverse_request_t *request, const char *buffer, reverse_response_t *response) {
	return reverse_fd->send_request(request, buffer, response);
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

bool pseudo_fd::is_readable(size_t &nbytes, bool &hangup)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::is_readable;
	request.inode = 0;
	request.flags = 0;
	request.offset = pos;
	request.recv_length = 0;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
		return false;
	} else {
		error = 0;
		nbytes = response.recv_length;
		hangup = response.flags & CLOUDABI_EVENT_FD_READWRITE_HANGUP;
		return response.result == 1;
	}
}

cloudabi_errno_t pseudo_fd::get_read_signaler(thread_condition_signaler **s)
{
	reverse_fd->subscribe_fd_read_events(shared_from_this());
	*s = &recv_signaler;
	return 0;
}

void pseudo_fd::became_readable()
{
	recv_signaler.condition_broadcast();
}

size_t pseudo_fd::read(void *dest, size_t count)
{
	if(count > UINT16_MAX) {
		count = UINT16_MAX;
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
	request.flags = flags & CLOUDABI_FDFLAG_APPEND;
	request.offset = pos;
	request.send_length = size;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, str, &response));
	if(response.result < 0) {
		error = -response.result;
	} else if(flags & CLOUDABI_FDFLAG_APPEND) {
		pos = response.result;
		error = 0;
	} else {
		pos += size;
		error = 0;
	}
	return size;
}

void pseudo_fd::datasync()
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::datasync;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::sync()
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::sync;
	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::lookup(const char *file, size_t filelen, cloudabi_oflags_t oflags, cloudabi_filestat_t *filestat)
{
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return;
	}

	if(!is_valid_path(file, filelen)) {
		error = EINVAL;
		return;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::lookup;
	request.inode = 0;
	request.flags = oflags;
	request.send_length = filelen;
	request.recv_length = 0;
	reverse_response_t response;
	Blk b = send_request(&request, file, &response);

	if (response.result < 0) {
		maybe_deallocate(b);
		error = -response.result;
		return;
	}

	if (b.size < sizeof(cloudabi_filestat_t)) {
		error = EIO;
		maybe_deallocate(b);
		return;
	}

	memcpy(filestat, b.ptr, sizeof(cloudabi_filestat_t));
	maybe_deallocate(b);
	if (filestat->st_dev != device) {
		get_vga_stream() << "Pseudo FD powered filesystem changed device IDs\n";
		error = EIO;
		return;
	}
	if (filestat->st_ino != static_cast<unsigned long long>(response.result)) {
		get_vga_stream() << "Pseudo FD powered filesystem inconsistent in inodes\n";
		error = EIO;
		return;
	}

	error = 0;
}


shared_ptr<fd_t> pseudo_fd::inode_open(cloudabi_device_t st_dev, cloudabi_inode_t st_ino, const cloudabi_fdstat_t *fdstat)
{
	if (st_dev != device) {
		error = EINVAL;
		return nullptr;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::open;
	request.inode = st_ino;
	request.flags = 0;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
		return nullptr;
	}

	pseudofd_t new_pseudo_id = response.result;

	char new_name[sizeof(name)];
	strncpy(new_name, name, sizeof(new_name));
	strncat(new_name, "->", sizeof(new_name) - strlen(new_name) - 1);

	char buf[sizeof(name)];
	strncat(new_name, ui64toa_s(st_ino, buf, sizeof(buf), 10), sizeof(new_name) - strlen(new_name) - 1);

	auto new_fd = make_shared<pseudo_fd>(new_pseudo_id, reverse_fd, response.flags, fdstat->fs_flags, new_name);
	// TODO: check if the rights are actually obtainable before opening the file;
	// ignore those that don't apply to this filetype, return ENOTCAPABLE if not
	new_fd->flags = fdstat->fs_flags;
	error = 0;

	return new_fd;
}

cloudabi_inode_t pseudo_fd::file_create(const char *file, size_t filelen, cloudabi_filetype_t type)
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
	request.send_length = filelen;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, file, &response));
	if(response.result < 0) {
		error = -response.result;
		return 0;
	} else {
		error = 0;
		return response.result;
	}
}

size_t pseudo_fd::file_readlink(const char *path, size_t pathlen, char *dest, size_t destlen)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::readlink;
	request.inode = 0;
	request.flags = 0;
	request.offset = 0;
	request.recv_length = destlen;
	request.send_length = pathlen;
	reverse_response_t response;
	Blk buf = send_request(&request, path, &response);
	if(response.result < 0) {
		error = -response.result;
		maybe_deallocate(buf);
		return 0;
	}

	error = 0;
	if(response.send_length > destlen) {
		get_vga_stream() << "pseudo-fd filesystem returned more data than requested, dropping";
		response.send_length = destlen;
	}

	memcpy(dest, buf.ptr, response.send_length);
	maybe_deallocate(buf);
	return response.send_length;
}

void pseudo_fd::file_rename(const char *path1, size_t path1len, shared_ptr<fd_t> fd2, const char *path2, size_t path2len)
{
	pseudo_fd *fd2ps = dynamic_cast<pseudo_fd*>(fd2.get());
	if(fd2ps == nullptr) {
		error = CLOUDABI_EXDEV;
		return;
	}

	// must be on the same reverse FD
	if(reverse_fd != fd2ps->reverse_fd) {
		error = CLOUDABI_EXDEV;
		return;
	}

	// path1 cannot contain null characters, as they are used as delimiters
	for(size_t i = 0; i < path1len; ++i) {
		if(path1[i] == 0) {
			error = CLOUDABI_EINVAL;
			return;
		}
	}

	Blk path = allocate(path1len + path2len + 1);
	char *pathstr = reinterpret_cast<char*>(path.ptr);
	memcpy(pathstr, path1, path1len);
	pathstr[path1len] = 0;
	memcpy(pathstr + path1len + 1, path2, path2len);

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::rename;
	request.inode = 0;
	request.flags = fd2ps->pseudo_id;
	request.send_length = path.size;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, pathstr, &response));
	deallocate(path);
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::file_link(const char *path1, size_t path1len, shared_ptr<fd_t> fd2, const char *path2, size_t path2len)
{
	pseudo_fd *fd2ps = dynamic_cast<pseudo_fd*>(fd2.get());
	if(fd2ps == nullptr) {
		error = CLOUDABI_EXDEV;
		return;
	}

	// must be on the same reverse FD
	if(reverse_fd != fd2ps->reverse_fd) {
		error = CLOUDABI_EXDEV;
		return;
	}

	// path1 cannot contain null characters, as they are used as delimiters
	for(size_t i = 0; i < path1len; ++i) {
		if(path1[i] == 0) {
			error = CLOUDABI_EINVAL;
			return;
		}
	}

	Blk path = allocate(path1len + path2len + 1);
	char *pathstr = reinterpret_cast<char*>(path.ptr);
	memcpy(pathstr, path1, path1len);
	pathstr[path1len] = 0;
	memcpy(pathstr + path1len + 1, path2, path2len);

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::link;
	request.inode = 0;
	request.flags = fd2ps->pseudo_id;
	request.offset = 0;
	request.send_length = path.size;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, pathstr, &response));
	deallocate(path);
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::file_symlink(const char *path1, size_t path1len, const char *path2, size_t path2len)
{
	// path1 cannot contain null characters, as they are used as delimiters
	for(size_t i = 0; i < path1len; ++i) {
		if(path1[i] == 0) {
			error = CLOUDABI_EINVAL;
			return;
		}
	}

	Blk path = allocate(path1len + path2len + 1);
	char *pathstr = reinterpret_cast<char*>(path.ptr);
	memcpy(pathstr, path1, path1len);
	pathstr[path1len] = 0;
	memcpy(pathstr + path1len + 1, path2, path2len);

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::symlink;
	request.inode = 0;
	request.flags = 0;
	request.send_length = path.size;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, pathstr, &response));
	deallocate(path);
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
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

void pseudo_fd::file_stat_put(const char *path, size_t pathlen, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags)
{
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return;
	}

	Blk buffer = allocate(sizeof(*buf) + pathlen);
	char *bufstr = reinterpret_cast<char*>(buffer.ptr);
	memcpy(bufstr, buf, sizeof(*buf));
	memcpy(bufstr + sizeof(*buf), path, pathlen);

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_put;
	request.inode = fsflags;
	request.flags = 0;
	request.send_length = buffer.size;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, bufstr, &response));
	deallocate(buffer);
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::file_stat_fput(const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags)
{
	auto res = lookup_device_id();
	if(res != 0) {
		error = res;
		return;
	}

	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::stat_fput;
	request.inode = fsflags;
	request.flags = 0;
	request.send_length = sizeof(*buf);

	reverse_response_t response;
	maybe_deallocate(send_request(&request, reinterpret_cast<const char*>(buf), &response));
	if(response.result < 0) {
		error = -response.result;
	} else {
		error = 0;
	}
}

void pseudo_fd::file_allocate(cloudabi_filesize_t offset, cloudabi_filesize_t length)
{
	reverse_request_t request;
	request.pseudofd = pseudo_id;
	request.op = reverse_request_t::operation::allocate;
	request.inode = 0;
	request.offset = offset;
	request.flags = length;

	reverse_response_t response;
	maybe_deallocate(send_request(&request, nullptr, &response));
	if(response.result < 0) {
		error = -response.result;
	}
	error = 0;
}

size_t pseudo_fd::readdir(char *buf, size_t nbyte, cloudabi_dircookie_t cookie)
{
	// readdir is allowed to return either a full buffer of nbyte bytes, or
	// any smaller amount of dir entries. It's only allowed to return 0
	// bytes if the directory has no (more) entries, in which case cookie
	// must also be 0. We'll re-request extra entries until our own buffer
	// is full or there are no more entries.
	size_t written = 0;
	while(written < nbyte) {
		reverse_request_t request;
		request.pseudofd = pseudo_id;
		request.op = reverse_request_t::operation::readdir;
		request.flags = cookie;
		request.recv_length = nbyte - written;

		reverse_response_t response;
		Blk b = send_request(&request, nullptr, &response);
		if(response.result < 0) {
			error = -response.result;
			maybe_deallocate(b);
			return 0;
		} else if(b.size > request.recv_length) {
			// more data returned than expected
			error = EIO;
			maybe_deallocate(b);
			return 0;
		}

		// assume that consistent, correct data was returned: a number of full directory entries, and
		// possibly a truncated one at the end of the buffer if b.size == request.recv_length
		cookie = response.result;
		memcpy(buf + written, b.ptr, b.size);
		written += b.size;
		maybe_deallocate(b);

		if(cookie == 0) {
			// no more entries
			break;
		}
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
	error = 0;
}
