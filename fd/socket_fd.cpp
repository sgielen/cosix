#include "socket_fd.hpp"
#include "pipe_fd.hpp"
#include <memory/allocator.hpp>

using namespace cloudos;

socket_fd::socket_fd(pipe_fd *r, pipe_fd *w, const char *n)
: fd_t(CLOUDABI_FILETYPE_SOCKET_STREAM, n)
, readfd(r)
, writefd(w)
{
}

size_t socket_fd::read(size_t offset, void *dest, size_t count)
{
	return readfd->read(offset, dest, count);
}

void socket_fd::putstring(const char *str, size_t count)
{
	return writefd->putstring(str, count);
}

error_t socket_fd::socketpair(socket_fd **a, socket_fd **b, size_t capacity)
{
	pipe_fd *tx = get_allocator()->allocate<pipe_fd>();
	pipe_fd *rx = get_allocator()->allocate<pipe_fd>();
	new (tx) pipe_fd(capacity, "tx for socket");
	new (rx) pipe_fd(capacity, "rx for socket");

	*a = get_allocator()->allocate<socket_fd>();
	*b = get_allocator()->allocate<socket_fd>();
	new (*a) socket_fd(tx, rx, "socketpair tx socket");
	new (*b) socket_fd(rx, tx, "socketpair rx socket");

	return error_t::no_error;
}
