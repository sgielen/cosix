#pragma once

#include <cloudabi_types_common.h>
#include <stddef.h>
#include <stdint.h>
#include <memory/smart_ptr.hpp>
#include "../oslibc/error.h"
#include "../oslibc/string.h"
#include "global.hpp"

namespace cloudos {

struct thread_condition_signaler;

inline vga_stream &operator<<(vga_stream &s, cloudabi_filetype_t type) {
	switch(type) {
#define FT(N) case CLOUDABI_FILETYPE_##N: s << #N; break
	FT(UNKNOWN);
	FT(BLOCK_DEVICE);
	FT(CHARACTER_DEVICE);
	FT(DIRECTORY);
	FT(PROCESS);
	FT(REGULAR_FILE);
	FT(SHARED_MEMORY);
	FT(SOCKET_DGRAM);
	FT(SOCKET_STREAM);
	FT(SYMBOLIC_LINK);
#undef FT
	}
	return s;
}

/** CloudOS file descriptors
 *
 * In CloudOS, file descriptors are objects which refer to a running process,
 * an open file or one of several other kinds of open handles. A single file
 * descriptor is held by the kernel, referring to the init process. From
 * there, file descriptors are held as shared pointers to each other and
 * therefore form a directed graph. When no shared pointers reference a
 * file descriptor anymore, it is destructed, and therefore closed.
 */

struct fd_t {
	cloudabi_filetype_t type;
	cloudabi_fdflags_t flags = 0;

	/* If this device represents a filesystem, this number must
	 * be positie and unique for this filesystem
	 */
	cloudabi_device_t device = 0;

	char name[64]; /* for debugging */
	cloudabi_errno_t error = 0;

	virtual cloudabi_filesize_t seek(cloudabi_filedelta_t /*offset*/, cloudabi_whence_t /*whence*/) {
		// this is not a seekable fd. Inherit from seekable_fd_t to have
		// a seekable fd.
		error = EBADF;
		return 0;
	}

	/* For memory, pipes, files and sockets */
	virtual size_t read(void * /*dest*/, size_t /*count*/) {
		error = EINVAL;
		return 0;
	}
	virtual size_t write(const char * /*str*/, size_t /*count*/) {
		error = EINVAL;
		return 0;
	}
	virtual size_t pread(void * /*str*/, size_t /*count*/, size_t /*offset*/) {
		error = EPIPE;
		return 0;
	}
	virtual size_t pwrite(const char * /*str*/, size_t /*count*/, size_t /*offset*/) {
		error = EPIPE;
		return 0;
	}

	// Returns EPIPE if the file descriptor will never become readable, e.g. because the
	// other end of the socket is shut down.
	virtual cloudabi_errno_t get_read_signaler(thread_condition_signaler **s) {
		*s = nullptr;
		return EINVAL;
	}

	// Returns EPIPE if the file descriptor will never become writable, e.g. because the
	// socket is shut down.
	virtual cloudabi_errno_t get_write_signaler(thread_condition_signaler **s) {
		*s = nullptr;
		return EINVAL;
	}

	/* For directories */
	/** Open a path. In the cloudabi_fdstat_t, rights_base and rights_inheriting specify the
	 * initial rights of the newly created file descriptor. The rights that do not apply to
	 * the filetype that will be opened (e.g. RIGHT_FD_SEEK on a socket) must be removed without
	 * error; rights that do apply to it but are unobtainable (e.g. RIGHT_FD_WRITE on a read-only
	 * filesystem) must generate ENOTCAPABLE. fs_flags specifies the initial flags of the fd; the
	 * filetype is ignored.
	 */
	virtual shared_ptr<fd_t> openat(const char * /*path */, size_t /*pathlen*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) {
		error = EINVAL;
		return nullptr;
	}

	/** Write directory entries to the given buffer, until it is filled. Each entry consists of a
	 * cloudabi_dirent_t object, follwed by cloudabi_dirent_t::d_namlen bytes holding the name of the
	 * entry. As much of the output buffer as possible is filled, potentially truncating the last entry.
	 * This allows the caller to grow its read buffer in case it's too small, and also noticing that
	 * the end of the directory was reached if the size returned < the nbyte given.
	 */
	virtual size_t readdir(char * /*buf*/, size_t /*nbyte*/, cloudabi_dircookie_t /*cookie*/) {
		error = ENOTDIR;
		return 0;
	}

	/** Create a path of given type. Returns the inode if no error is set.
	 */
	virtual cloudabi_inode_t file_create(const char * /*path*/, size_t /*pathlen*/, cloudabi_filetype_t /*type*/)
	{
		error = EINVAL;
		return 0;
	}

	/** Unlinks a path of given type.
	 */
	virtual void file_unlink(const char * /*path*/, size_t /*pathlen*/, cloudabi_ulflags_t /*flags*/)
	{
		error = EINVAL;
	}

	/** Get attributes of a file by path.
	 */
	virtual void file_stat_get(cloudabi_lookupflags_t /*flags*/, const char* path, size_t pathlen, cloudabi_filestat_t* buf)
	{
		// If file_stat_get is unimplemented, use openat() + file_stat_fget
		cloudabi_fdstat_t stat;
		auto fd = openat(path, pathlen, 0, &stat);
		if(error) {
			return;
		}
		fd->file_stat_fget(buf);
		if(fd->error) {
			error = fd->error;
		}
	}

	/** Get attributes of the open file.
	 */
	virtual void file_stat_fget(cloudabi_filestat_t *buf)
	{
		// fill in what we know; override this to give all values
		buf->st_dev = device;
		buf->st_ino = 0;
		buf->st_filetype = type;
		buf->st_nlink = 0;
		buf->st_size = 0;
		buf->st_atim = 0;
		buf->st_mtim = 0;
		buf->st_ctim = 0;
		error = 0;
	}

	/* For sockets */
	virtual void sock_shutdown(cloudabi_sdflags_t /*how*/)
	{
		error = ENOTSOCK;
	}

	virtual void sock_recv(const cloudabi_recv_in_t* /*in*/, cloudabi_recv_out_t* /*out*/)
	{
		error = ENOTSOCK;
	}

	virtual void sock_send(const cloudabi_send_in_t* /*in*/, cloudabi_send_out_t* /*out*/)
	{
		error = ENOTSOCK;
	}

	virtual ~fd_t() {}

protected:
	inline fd_t(cloudabi_filetype_t t, const char *n) : type(t) {
		strncpy(name, n, sizeof(name));
		name[sizeof(name)-1] = 0;
	}
};

struct seekable_fd_t : public fd_t {
	cloudabi_filesize_t pos;

	virtual size_t pread(void *str, size_t count, size_t offset) override {
		auto oldpos = pos;
		error = 0;
		seek(offset, CLOUDABI_WHENCE_SET);
		if(error) {
			return 0;
		}
		assert(pos == offset);
		auto readcnt = read(str, count);
		auto read_error = error;

		seek(oldpos, CLOUDABI_WHENCE_SET);
		if(read_error) {
			error = read_error;
		}
		return readcnt;
	}

	virtual size_t pwrite(const char *str, size_t count, size_t offset) override {
		auto oldpos = pos;
		error = 0;
		seek(offset, CLOUDABI_WHENCE_SET);
		if(error) {
			return 0;
		}
		assert(pos == offset);
		auto written = write(str, count);
		auto write_error = error;

		seek(oldpos, CLOUDABI_WHENCE_SET);
		if(write_error) {
			error = write_error;
		}
		return written;
	}

	virtual cloudabi_filesize_t seek(cloudabi_filedelta_t offset, cloudabi_whence_t whence) override {
		if(whence == CLOUDABI_WHENCE_CUR) {
			if(offset > 0 && static_cast<uint64_t>(offset) > (UINT64_MAX - pos)) {
				// prevent overflow
				error = EOVERFLOW;
			} else if(offset < 0 && static_cast<uint64_t>(-offset) > pos) {
				// prevent underflow
				error = EINVAL;
			} else {
				// note that pos > size is allowed
				error = 0;
				pos = pos + offset;
			}
		} else if(whence == CLOUDABI_WHENCE_END) {
			// TODO: this needs to obtain the filesize, so it needs file_stat
			error = ENOSYS;
			get_vga_stream() << "CLOUDABI_WHENCE_END not supported yet in seek\n";
			/*
			cloudabi_filesize_t size = stat(...);
			if(offset > (UINT64_MAX - size)) {
				error = EOVERFLOW;
			} else if(offset < 0 && -offset > size) {
				error = EINVAL;
			} else {
				pos = size + offset;
			}
			*/
		} else if(whence == CLOUDABI_WHENCE_SET) {
			if(offset < 0) {
				error = EINVAL;
			} else {
				error = 0;
				pos = offset;
			}
		} else {
			// invalid whence
			error = EINVAL;
		}
		return pos;
	}

protected:
	inline seekable_fd_t(cloudabi_filetype_t t, const char *n) : fd_t(t, n), pos(0) {
	}
};


};

