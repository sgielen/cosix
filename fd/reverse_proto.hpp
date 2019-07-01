#pragma once

namespace reverse_proto {

typedef uint64_t pseudofd_t;

struct reverse_request_t {
	pseudofd_t pseudofd = 0;
	enum class operation {
		lookup = 0, // filename in buffer, oflags in flags, returns inode in result and filestat_t in buffer
		stat_fget,
		stat_put, // fsflags in inode, buffer is filestat_t* + filename
		stat_fput, // fsflags in inode, buffer is filestat_t*
		is_readable,
		datasync,
		sync,
		// all the calls below use the inode, not the filename
		open,
		create, // filename in buffer
		allocate, // length in flags
		readdir, // cookie in result (0 if last entry), put a cloudabi_dirent_t + name in buffer
		readlink,
		rename, // send fd2 as 'flags', and buffer is 'filename1<nullbyte>filename2'
		symlink, // buffer is 'filename1<nullbyte>filename2'
		link, // send fd2 as 'flags', and buffer is 'filename1<nullbyte>filename2'
		unlink,
		pread,
		pwrite, // if flags is CLOUDABI_FDFLAG_APPEND, ignore offset, always append, return pos in result
		close,
		// the calls below are for UNIX sockets; inode is 0
		sock_shutdown,
		sock_recv,
		sock_send
	} op;
	uint64_t inode = 0;
	uint64_t flags = 0;
	uint64_t offset = 0;
	uint16_t send_length = 0; // bytes following this request (for filenames & writes)
	uint16_t recv_length = 0; // length to read
};

struct reverse_response_t {
	int64_t result = 0; // < 0 is -errno, 0 is success, >= 0 is result (can be inode or pseudo-fd)
	uint64_t flags = 0; // filetype in case of lookup/open
	bool gratituous = false;
	uint16_t send_length = 0; // bytes following this response
	uint16_t recv_length = 0; // bytes actually read
};

}
