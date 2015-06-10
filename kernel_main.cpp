#include "hw/vga.hpp"

using namespace cloudos;

#if defined(__cplusplus)
extern "C"
#endif
void kernel_main() {
	vga_buffer buf;
	buf.write("Hello world!\n");
}
