#include "pipe_fd.hpp"

using namespace cloudos;

pipe_fd::pipe_fd(size_t c, const char *n)
: fd_t(CLOUDABI_FILETYPE_FIFO, n)
, buffer(nullptr)
, used(0)
, capacity(c)
{
	Blk b = allocate(capacity);
	buffer = reinterpret_cast<char*>(b.ptr);
}

pipe_fd::~pipe_fd()
{
	deallocate({buffer, capacity});
}

size_t pipe_fd::read(void *dest, size_t count)
{
	// count > capacity is no problem, we limit to the used size
	error = 0;

	while(used == 0) {
		readcv.wait();
	}
	size_t num_bytes = count <= used ? count : used;
	memcpy(dest, buffer, num_bytes);

	size_t remaining_bytes = used - num_bytes;
	if(remaining_bytes > 0) {
		memcpy(buffer, buffer + num_bytes, remaining_bytes);
	}
	used = remaining_bytes;
	writecv.broadcast();
	return num_bytes;
}

size_t pipe_fd::write(const char *str, size_t count)
{
	if(count > capacity) {
		count = capacity;
	}

	while(used + count > capacity) {
		writecv.wait();
	}

	memcpy(buffer + used, str, count);
	used += count;
	readcv.broadcast();
	error = 0;
	return count;
}
