#pragma once

#include "fd.hpp"
#include <concur/cv.hpp>

namespace cloudos {

/**
 * A pipe/fifo FD.
 *
 * This FD implements a FIFO pipe: anything written to it, can also be read
 * from it. Normally, this FD is added to a process twice: once in read mode,
 * once in write mode -- but this is not enforced by the pipe_fd
 * implementation.
 */
struct pipe_fd : fd_t {
	pipe_fd(size_t capacity, const char *n);
	~pipe_fd() override;

	/** read() blocks until at least 1 byte of data is available;
	 * then, it returns up to count bytes of data in the dest buffer.
	 * It sets invalid_argument as the error if offset is not 0.
	 */
	size_t read(void *dest, size_t count) override;

	/** write() blocks until there is capacity for at least count
	 * bytes, then, it appends the given buffer to the stored one.
	 */
	size_t write(const char * /*str*/, size_t /*count*/) override;

private:
	char *buffer;
	size_t used;
	size_t capacity;

	cv_t readcv;
	cv_t writecv;
};

}
