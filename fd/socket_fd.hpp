#pragma once

#include "fd.hpp"

namespace cloudos {

struct pipe_fd;

/**
 * A socket FD.
 *
 * This FD consists of a pipe (fifo) FD to read from, and one to write to.
 * This way, it implements bidirectional communication using the FIFO building
 * block. Use the socketpair() factory function to build a complete socket
 * pair from scratch.
 */
struct socket_fd : fd_t {
	socket_fd(pipe_fd *read, pipe_fd *write, const char *n);

	/** read() blocks until at least 1 byte of data is available;
	 * then, it returns up to count bytes of data in the dest buffer.
	 * It sets invalid_argument as the error if offset is not 0.
	 */
	size_t read(size_t offset, void *dest, size_t count) override;

	/** putstring() blocks until there is capacity for at least count
	 * bytes, then, it appends the given buffer to the stored one.
	 */
	void putstring(const char * /*str*/, size_t /*count*/) override;

	/** Creates two socket_fd's that form a connected pair. */
	static error_t socketpair(socket_fd **a, socket_fd **b, size_t capacity);

private:
	pipe_fd *readfd;
	pipe_fd *writefd;
};

}
