#include "fd.hpp"

namespace cloudos {

/** VGA stream file descriptor
 *
 * This fd is write-only. If something is written to it, this appears on
 * the VGA console as-is.
 */
struct vga_fd : public fd_t {
	inline vga_fd(const char *n) : fd_t(CLOUDABI_FILETYPE_FIFO, n) {}

	inline error_t putstring(const char *str, size_t count) override {
		for(size_t i = 0; i < count; ++i) {
			get_vga_stream() << str[i];
		}
		error = error_t::no_error;
		return error;
	}
};

}
