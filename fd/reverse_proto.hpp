#pragma once

namespace reverse_proto {

typedef uint64_t pseudofd_t;

struct reverse_request_t {
	pseudofd_t pseudofd = 0;
	enum class operation {
		lookup /* path in buffer -> inode */,
		stat_get,
		stat_fget,
		stat_put, // fsflags in inode, lookupflags in flags, buffer is filestat_t* + path
		stat_fput, // fsflags in inode, buffer is filestat_t*
		is_readable,
		// all the calls below use the inode, not the path
		open,
		create,
		allocate, // length in flags
		readdir, // cookie in result (0 if last entry), put a cloudabi_dirent_t + name in buffer
		readlink,
		rename, // send fd2 as 'flags', and buffer is 'path1<nullbyte>path2'
		symlink, // buffer is 'path1<nullbyte>path2'
		link, // offset is lookupflags, send fd2 as 'flags', and buffer is 'path1<nullbyte>path2'
		unlink,
		pread,
		pwrite,
		close,
		// the calls below are for UNIX sockets; inode is 0
		sock_shutdown,
		sock_recv,
		sock_send
	} op;
	uint64_t inode = 0;
	uint64_t flags = 0;
	uint64_t offset = 0;
	uint16_t send_length = 0; // bytes following this request (for paths & writes)
	uint16_t recv_length = 0; // length to read
};

struct reverse_response_t {
	int64_t result = 0; // < 0 is -errno, 0 is success, >= 0 is result (can be inode or pseudo-fd)
	uint64_t flags = 0; // filetype in case of lookup
	bool gratituous = false;
	uint16_t send_length = 0; // bytes following this response
	uint16_t recv_length = 0; // bytes actually read
};

}
