#include "hw/vga.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/numeric.h"
#include "cloudos_version.h"

using namespace cloudos;

#if defined(__cplusplus)
extern "C"
#endif
void kernel_main() {
	vga_buffer buf;
	vga_stream stream(buf);
	stream << "CloudOS v" cloudos_VERSION " -- starting up\n";
	stream << "Multiboot magic: " << hex << 0x2BADB002 << "\n";

	stream << "Shutting down\n";
}
