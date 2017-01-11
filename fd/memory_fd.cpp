#include "memory_fd.hpp"

using namespace cloudos;

memory_fd::memory_fd(const char *n)
: seekable_fd_t(CLOUDABI_FILETYPE_REGULAR_FILE, n)
, alloc({nullptr, 0})
, file_length(0)
, owned(false)
{}

memory_fd::memory_fd(Blk a, size_t l, const char *n)
: memory_fd(n)
{
	reset(a, l);
}

memory_fd::memory_fd(void *a, size_t l, const char *n)
: memory_fd(n)
{
	reset(a, l);
}

void memory_fd::reset() {
	alloc = {nullptr, 0};
	file_length = 0;
	owned = false;
}

void memory_fd::reset(Blk a, size_t l) {
	alloc = a;
	file_length = l;
	owned = true;
}

void memory_fd::reset(void *a, size_t l) {
	alloc = {a, 0};
	file_length = l;
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
		error = EAGAIN;
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

