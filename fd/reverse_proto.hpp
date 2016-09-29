#pragma once

namespace reverse_proto {

typedef uint64_t pseudofd_t;

struct reverse_request_t {
	pseudofd_t pseudofd;
	enum class operation {
		lookup /* path in buffer -> inode */,
		// all the calls below use the inode, not the path
		stat_get,
		stat_put,
		open,
		create,
		readdir, // cookie in result (0 if last entry), put a cloudabi_dirent_t + name in buffer
		rename,
		unlink,
		pread,
		pwrite,
		close
	} op;
	uint64_t inode;
	uint64_t flags;
	uint64_t offset;
	uint8_t length; // bytes used in buffer (for paths and writes), otherwise, length to read
	uint8_t buffer[256];
};

struct reverse_response_t {
	int64_t result; // < 0 is -errno, 0 is success, >= 0 is result (can be inode or pseudo-fd)
	uint64_t flags; // filetype in case of lookup
	uint8_t length; // bytes used in buffer (bytes actually read)
	uint8_t buffer[256];
};

}
