#include "hw/vga.hpp"
#include "cloudos_version.h"

using namespace cloudos;

#if defined(__cplusplus)
extern "C"
#endif
void kernel_main() {
	vga_buffer buf;
	buf.write("CloudOS v" cloudos_VERSION " -- starting up");
}
