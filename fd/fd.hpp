#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../oslibc/error.h"
#include "../oslibc/string.h"
#include "global.hpp"

namespace cloudos {

enum class fd_type_t {
	memory,
	process,
	file,
	directory,
	pipe
};

inline vga_stream &operator<<(vga_stream &s, fd_type_t type) {
	switch(type) {
	case fd_type_t::memory: s << "memory"; break;
	case fd_type_t::process: s << "process"; break;
	case fd_type_t::file: s << "file"; break;
	case fd_type_t::directory: s << "directory"; break;
	case fd_type_t::pipe: s << "pipe"; break;
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
	fd_type_t type;

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
	virtual fd_t *openat(const char * /*pathname*/, bool /*directory*/) {
		error = error_t::invalid_argument;
		return nullptr;
	}

protected:
	inline fd_t(fd_type_t t, const char *n) : type(t), refcount(1), invalid(false), error(error_t::no_error) {
		strncpy(name, n, sizeof(name));
		name[sizeof(name)-1] = 0;
	}

	virtual ~fd_t() {}
};

};

