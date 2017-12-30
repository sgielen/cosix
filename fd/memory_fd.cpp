#include "memory_fd.hpp"

using namespace cloudos;

memory_fd::memory_fd(const char *n, cloudabi_inode_t i)
: seekable_fd_t(CLOUDABI_FILETYPE_REGULAR_FILE, 0, n)
, alloc({nullptr, 0})
, file_length(0)
, inode(i)
, owned(false)
{}

memory_fd::memory_fd(Blk a, size_t l, const char *n, cloudabi_inode_t i)
: memory_fd(n, i)
{
	reset(a, l);
}

memory_fd::memory_fd(void *a, size_t l, const char *n, cloudabi_inode_t i)
: memory_fd(n, i)
{
	reset(a, l);
}

void memory_fd::reset() {
	alloc = {nullptr, 0};
	file_length = 0;
	inode = 0;
	owned = false;
}

void memory_fd::reset(Blk a, size_t l, cloudabi_inode_t i) {
	alloc = a;
	file_length = l;
	inode = i;
	owned = true;
}

void memory_fd::reset(void *a, size_t l, cloudabi_inode_t i) {
	alloc = {a, 0};
	file_length = l;
	inode = i;
	owned = false;
}

memory_fd::~memory_fd()
{
	if(owned) {
		deallocate(alloc);
	}
}

size_t memory_fd::read(void *dest, size_t count) {
	error = 0;
	if(pos >= file_length) {
		// EOF, don't change dest
		return 0;
	}

	size_t bytes_left = file_length - pos;
	size_t copied = count < bytes_left ? count : bytes_left;
	if(copied != 0) {
		assert(dest != nullptr);
		assert(alloc.ptr != nullptr);
		memcpy(reinterpret_cast<char*>(dest), reinterpret_cast<char*>(alloc.ptr) + pos, copied);
		pos += copied;
	}
	error = 0;
	return copied;
}

void memory_fd::file_stat_fget(cloudabi_filestat_t *buf) {
	buf->st_dev = device;
	buf->st_ino = inode;
	buf->st_filetype = type;
	buf->st_nlink = 1;
	buf->st_size = file_length;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
	error = 0;
}
