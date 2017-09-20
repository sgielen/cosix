#include "fd.hpp"

namespace cloudos {

/** VGA stream file descriptor
 *
 * This fd is write-only. If something is written to it, this appears on
 * the VGA console as-is.
 */
struct vga_fd : public fd_t {
	inline vga_fd(const char *n) : fd_t(CLOUDABI_FILETYPE_CHARACTER_DEVICE, n) {}

	inline size_t write(const char *str, size_t count) override {
		for(size_t i = 0; i < count; ++i) {
			get_vga_stream() << str[i];
		}
		error = 0;
		return count;
	}
};

}
