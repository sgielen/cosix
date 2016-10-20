#pragma once

#include <cloudabi_types_common.h>
#include <stddef.h>
#include <stdint.h>
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
	cloudabi_fdflags_t flags;

	size_t refcount;
	bool invalid;

	char name[64]; /* for debugging */
	cloudabi_errno_t error;

	/* For memory, pipes and files */
	virtual size_t read(size_t /*offset*/, void * /*dest*/, size_t /*count*/) {
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
	virtual fd_t *openat(const char * /*path */, size_t /*pathlen*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) {
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
		error = EINVAL;
		return 0;
	}

	/** Create a path of given type.
	 */
	virtual void file_create(const char * /*path*/, size_t /*pathlen*/, cloudabi_filetype_t /*type*/)
	{
		error = EINVAL;
	}

protected:
	inline fd_t(cloudabi_filetype_t t, const char *n) : type(t), flags(0), refcount(1), invalid(false), error(0) {
		strncpy(name, n, sizeof(name));
		name[sizeof(name)-1] = 0;
	}

	virtual ~fd_t() {}
};

};

