#include "hw/vga.hpp"
#include "oslibc/numeric.h"
#include "cloudos_version.h"

using namespace cloudos;

#if defined(__cplusplus)
extern "C"
#endif
void kernel_main() {
	vga_buffer buf;
	buf.write("CloudOS v" cloudos_VERSION " -- starting up\n");
	char digits[40];
	buf.write("Multiboot magic: 0x");
	buf.write(ui64toa_s(0x2BADB002, &digits[0], sizeof(digits), 16));
	buf.putc('\n');

	buf.write("Shutting down\n");
}
