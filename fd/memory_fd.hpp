#include "fd.hpp"

namespace cloudos {

/** Memory file descriptor
 *
 * This file descriptor takes ownership of the given buffer. Reads on this
 * file descriptor will read from the buffer. Writes are not supported.
 */
struct memory_fd : public fd_t {
	inline memory_fd(char *a, size_t l, const char *n) : fd_t(CLOUDABI_FILETYPE_SHARED_MEMORY, n), addr(a), length(l) {}

	size_t read(size_t offset, void *dest, size_t count) override {
		error = 0;
		if(offset + count > length) {
			// EOF, don't change dest
			return 0;
		}

		size_t bytes_left = length - offset;
		size_t copied = count < bytes_left ? count : bytes_left;
		memcpy(reinterpret_cast<char*>(dest), addr + offset, copied);
		return copied;
	}

private:
	char *addr;
	size_t length;
};

}
