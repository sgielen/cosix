#include "socket_fd.hpp"
#include "pipe_fd.hpp"
#include <memory/allocator.hpp>

using namespace cloudos;

socket_fd::socket_fd(shared_ptr<pipe_fd> r, shared_ptr<pipe_fd> w, const char *n)
: fd_t(CLOUDABI_FILETYPE_SOCKET_STREAM, n)
, readfd(r)
, writefd(w)
{
}

size_t socket_fd::read(void *dest, size_t count)
{
	auto res = readfd->read(dest, count);
	error = readfd->error;
	return res;
}

void socket_fd::putstring(const char *str, size_t count)
{
	writefd->putstring(str, count);
	error = writefd->error;
}

void socket_fd::socketpair(shared_ptr<socket_fd> &a, shared_ptr<socket_fd> &b, size_t capacity)
{
	auto tx = make_shared<pipe_fd>(capacity, "tx for socket");
	auto rx = make_shared<pipe_fd>(capacity, "rx for socket");

	a = make_shared<socket_fd>(tx, rx, "socketpair tx socket");
	b = make_shared<socket_fd>(rx, tx, "socketpair rx socket");
}
