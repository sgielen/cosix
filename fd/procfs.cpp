#include "procfs.hpp"
#include "global.hpp"
#include <fd/memory_fd.hpp>
#include <oslibc/numeric.h>
#include <memory/allocator.hpp>
#include <time/clock_store.hpp>

using namespace cloudos;

static const int PROCFS_DEPTH_MAX = 5;
static const int PROCFS_FILE_MAX = 40;

namespace cloudos {

struct procfs_directory_fd : fd_t {
	procfs_directory_fd(const char (*)[PROCFS_FILE_MAX], const char *n);

	bool to_string(char *buf, size_t length);

	shared_ptr<fd_t> openat(const char * /*path */, size_t /*pathlen*/, cloudabi_lookupflags_t /*lookupflags*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) override;

private:
	char path[PROCFS_DEPTH_MAX][PROCFS_FILE_MAX];
	size_t depth;
};

struct procfs_uptime_fd : public memory_fd {
	procfs_uptime_fd(const char *n) : memory_fd(n) {}

	size_t read(void *dest, size_t count) override;
};

struct procfs_alloctrack_fd : public fd_t {
	procfs_alloctrack_fd(const char *n) : fd_t(CLOUDABI_FILETYPE_REGULAR_FILE, n) {}

	size_t write(const char *buf, size_t count) override;
};

}

procfs_directory_fd::procfs_directory_fd(const char (*p)[PROCFS_FILE_MAX], const char *n)
: fd_t(CLOUDABI_FILETYPE_DIRECTORY, n)
{
	for(depth = 0; depth < PROCFS_DEPTH_MAX; ++depth) {
		if(p[depth] && p[depth][0]) {
			strncpy(path[depth], p[depth], PROCFS_FILE_MAX);
		} else {
			break;
		}
	}
	if(depth == PROCFS_DEPTH_MAX) {
		kernel_panic("procfs_directory_fd too deep, but already in constructor");
	}
	for(size_t i = depth; i < PROCFS_DEPTH_MAX; ++i) {
		path[i][0] = 0;
	}

	char pathbuf[PROCFS_DEPTH_MAX * (PROCFS_FILE_MAX + 1)];
	if(to_string(pathbuf, sizeof(pathbuf))) {
		get_vga_stream() << "A procfs_directory_fd was created with path: " << pathbuf << "\n";
	} else {
		get_vga_stream() << "A procfs_directory_fd was created but path didn't fit?\n";
	}
}

bool procfs_directory_fd::to_string(char *buf, size_t length) {
	size_t bytes_written = 0;
	buf[0] = 0;
	for(size_t i = 0; i < depth; ++i) {
		size_t elemlen = strlen(path[i]); // excluding the ending nullbyte
		size_t length_left = length - bytes_written; // including the ending nullbyte
		if(elemlen >= length_left || (i != depth && elemlen >= length_left - 1)) {
			// no memory to store this or the next path item
			strncpy(&buf[bytes_written], path[i], length_left - 1);
			buf[length] = 0;
			return false;
		}
		strncpy(&buf[bytes_written], path[i], elemlen);
		bytes_written += elemlen;
		if(i != depth) {
			// a path follows
			buf[bytes_written] = '/';
			++bytes_written;
		}
		buf[bytes_written] = 0;
	}
	return true;
}

shared_ptr<fd_t> procfs_directory_fd::openat(const char *pathname, size_t pathlen, cloudabi_lookupflags_t, cloudabi_oflags_t oflags, const cloudabi_fdstat_t *) {
	if(pathname == nullptr || pathlen == 0 || pathname[0] == 0 || pathname[0] == '/') {
		error = EINVAL;
		return nullptr;
	}

	char pathbuf[PROCFS_DEPTH_MAX * (PROCFS_FILE_MAX + 1)];
	if(!to_string(pathbuf, sizeof(pathbuf))) {
		kernel_panic("to_string() should always succeed");
	}
	size_t pathbuf_length = strlen(pathbuf);

	if(pathlen + 1 + pathbuf_length >= sizeof(pathbuf)) {
		error = ENOMEM;
		return nullptr;
	}

	bool must_be_directory = (oflags & CLOUDABI_O_DIRECTORY) == CLOUDABI_O_DIRECTORY;

	// TODO: look at oflags and fdstat
	// TODO: actual path resolving with './', '../', symlinks and hardlinks
	strncpy(&pathbuf[pathbuf_length], pathname, pathlen + 1);

	if(strcmp(pathbuf, "kernel/uptime") == 0) {
		if(must_be_directory) {
			error = ENOTDIR;
			return nullptr;
		} else {
			error = 0;
			return make_shared<procfs_uptime_fd>(pathbuf);
		}
	} else if(strcmp(pathbuf, "kernel/alloctracker") == 0) {
		if(must_be_directory) {
			error = ENOTDIR;
			return nullptr;
		} else {
			error = 0;
			return make_shared<procfs_alloctrack_fd>(pathbuf);
		}
	} else if(strcmp(pathbuf, "kernel") == 0 || strcmp(pathbuf, "kernel/") == 0) {
		error = 0;
		char pb[2][PROCFS_FILE_MAX];
		strncpy(pb[0], "kernel", PROCFS_FILE_MAX);
		pb[1][0] = 0;
		return make_shared<procfs_directory_fd>(pb, "procfs_kernel_dir");
	} else {
		error = ENOENT;
		return nullptr;
	}
}

size_t procfs_uptime_fd::read(void *dest, size_t count) {
	auto uptime = get_clock_store()->get_clock(CLOUDABI_CLOCK_MONOTONIC)->get_time(0) / 1e9;
	char buf[10];

	char *addr = ui64toa_s(uptime, buf, sizeof(buf), 10);
	reset(addr, strlen(addr));
	auto res = memory_fd::read(dest, count);
	reset();
	return res;
}

size_t procfs_alloctrack_fd::write(const char *buf, size_t count) {
	(void)buf;
	error = 0;
	// TODO: static_assert 'if get_allocator()->get_allocator()->start_tracking() exists'
#ifndef NDEBUG
	if(count == 0) {
		return 0;
	}
	char b = buf[0];
	if(b == '1') {
		get_vga_stream() << "====== allocation tracking turned on =======\n";
		get_allocator()->get_allocator()->start_tracking();
	} else if(b == '0') {
		get_vga_stream() << "====== allocation tracking turned off =======\n";
		get_allocator()->get_allocator()->stop_tracking();
	} else if(b == 'R') {
		size_t num = get_allocator()->get_allocator()->dump_allocations();
		if(num == 0) {
			get_vga_stream() << "===== zero allocations still live =====\n";
		}
	}
#endif
	return count;
}

shared_ptr<fd_t> procfs::get_root_fd() {
	char pb[1][PROCFS_FILE_MAX];
	pb[0][0] = 0;
	return make_shared<procfs_directory_fd>(pb, "procfs_root");
}
