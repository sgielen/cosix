#pragma once

#include <cloudabi_types_common.h>
#include <stddef.h>
#include <stdint.h>
#include <memory/smart_ptr.hpp>
#include "../oslibc/error.h"
#include "../oslibc/string.h"
#include "global.hpp"

namespace cloudos {

inline vga_stream &operator<<(vga_stream &s, cloudabi_filetype_t type) {
	switch(type) {
#define FT(N) case CLOUDABI_FILETYPE_##N: s << #N; break
	FT(UNKNOWN);
	FT(BLOCK_DEVICE);
	FT(CHARACTER_DEVICE);
	FT(DIRECTORY);
	FT(FIFO);
	FT(POLL);
	FT(PROCESS);
	FT(REGULAR_FILE);
	FT(SHARED_MEMORY);
	FT(SOCKET_DGRAM);
	FT(SOCKET_SEQPACKET);
	FT(SOCKET_STREAM);
	FT(SYMBOLIC_LINK);
#undef FT
	}
	return s;
}

/** CloudOS file descriptors
 *
 * In CloudOS, file descriptors are refcounted objects which refer to a running
 * process, an open file or one of several other kinds of open handles. A
 * single file descriptor is held by the kernel, referring to the init process.
 * From there, file descriptors form a directed acyclic graph.
 */

struct fd_t {
	cloudabi_filetype_t type;
	cloudabi_fdflags_t flags = 0;

	/* If this device represents a filesystem, this number must
	 * be positie and unique for this filesystem
	 */
	cloudabi_device_t device = 0;

	size_t refcount = 1;
	bool invalid = false;

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
	virtual void putstring(const char * /*str*/, size_t /*count*/) {
		error = EINVAL;
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
	virtual void file_stat_get(cloudabi_lookupflags_t /*flags*/, const char* /*path*/, size_t /*path_len*/, cloudabi_filestat_t* /*buf*/)
	{
		error = EINVAL;
	}

	/* For sockets */
	virtual void sock_bind(cloudabi_sa_family_t /*family*/, shared_ptr<fd_t> /*fd*/, void * /*address*/, size_t /*address_len*/)
	{
		error = ENOTSOCK;
	}

	virtual void sock_connect(cloudabi_sa_family_t /*family*/, shared_ptr<fd_t> /*fd*/, void * /*address*/, size_t /*address_len*/)
	{
		error = ENOTSOCK;
	}

	virtual void sock_listen(cloudabi_backlog_t /*backlog*/)
	{
		error = ENOTSOCK;
	}

	virtual shared_ptr<fd_t> sock_accept(cloudabi_sa_family_t /*family*/, void * /*address*/, size_t* /*address_len*/)
	{
		error = ENOTSOCK;
		return nullptr;
	}

	virtual void sock_shutdown(cloudabi_sdflags_t /*how*/)
	{
		error = ENOTSOCK;
	}

	virtual void sock_stat_get(cloudabi_sockstat_t* /*buf*/, cloudabi_ssflags_t /*flags*/)
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

