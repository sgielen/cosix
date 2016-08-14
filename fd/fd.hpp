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
	error_t error;

	/* For memory, pipes and files */
	virtual size_t read(size_t /*offset*/, void * /*dest*/, size_t /*count*/) {
		error = error_t::invalid_argument;
		return 0;
	}
	virtual error_t putstring(const char * /*str*/, size_t /*count*/) {
		error = error_t::invalid_argument;
		return error;
	}

	/* For directories */
	virtual fd_t *openat(const char * /*path */, size_t /*pathlen*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) {
		error = error_t::invalid_argument;
		return nullptr;
	}

protected:
	inline fd_t(cloudabi_filetype_t t, const char *n) : type(t), flags(0), refcount(1), invalid(false), error(error_t::no_error) {
		strncpy(name, n, sizeof(name));
		name[sizeof(name)-1] = 0;
	}

	virtual ~fd_t() {}
};

};

