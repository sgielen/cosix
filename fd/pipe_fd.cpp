#include "pipe_fd.hpp"
#include <memory/allocator.hpp>

using namespace cloudos;

pipe_fd::pipe_fd(size_t c, const char *n)
: fd_t(CLOUDABI_FILETYPE_FIFO, n)
, buffer(nullptr)
, used(0)
, capacity(c)
{
	buffer = get_allocator()->allocate<char>(capacity);
}

size_t pipe_fd::read(size_t offset, void *dest, size_t count)
{
	// count > capacity is no problem, we limit to the used size

	if(offset != 0) {
		error = error_t::invalid_argument;
		return 0;
	}
	error = error_t::no_error;

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

error_t pipe_fd::putstring(const char *str, size_t count)
{
	if(count > capacity) {
		// TODO: write only a part
		error = error_t::invalid_argument;
		return error;
	}

	while(used + count > capacity) {
		writecv.wait();
	}

	memcpy(buffer + used, str, count);
	used += count;
	readcv.broadcast();
	error = error_t::no_error;
	return error;
}
