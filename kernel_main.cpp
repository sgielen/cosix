#include "hw/vga.hpp"
#include "hw/vga_stream.hpp"
#include "hw/multiboot.hpp"
#include "oslibc/numeric.h"
#include "cloudos_version.h"

using namespace cloudos;

#if defined(__cplusplus)
extern "C"
#endif
void kernel_main(uint32_t multiboot_magic, void *bi_ptr) {
	vga_buffer buf;
	vga_stream stream(buf);
	stream << "CloudOS v" cloudos_VERSION " -- starting up\n";

	multiboot_info boot_info(bi_ptr, multiboot_magic);

	if(!boot_info.is_valid()) {
		stream << "Panic: CloudOS must be booted by a Multiboot capable loader.\n";
		return;
	}

	uint32_t mem_lower, mem_upper;
	if(boot_info.mem_amount(&mem_lower, &mem_upper)) {
		stream << "Amount of lower memory (kilobytes): " << mem_lower << "\n";
		stream << "Amount of upper memory (kilobytes): " << mem_upper << "\n";
	}

	stream << "Shutting down\n";
}
