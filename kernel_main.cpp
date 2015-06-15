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

	memory_map_entry *mmap;
	size_t memory_map_bytes = boot_info.memory_map(&mmap);
	if(memory_map_bytes == 0) {
		stream << "Panic: Missing memory map information from the bootloader.\n";
		return;
	}

	for(size_t i = 0; memory_map_bytes > 0; ++i) {
		memory_map_entry &entry = mmap[i];
		if(entry.entry_size == 0) {
			stream << "  " << i << ": end of list\n";
			break;
		} else if(entry.entry_size != 20) {
			stream << "Memory map entry " << i << " size is invalid: " << entry.entry_size << "\n";
			break;
		}
		memory_map_bytes -= entry.entry_size;

		uint64_t begin_addr = reinterpret_cast<uint64_t>(entry.mem_base.addr);
		uint64_t end_addr = begin_addr + entry.mem_length;

		stream  << "  " << i << ": Addr " << hex << begin_addr
			<< " to " << end_addr << " (" << dec
			<< "length " << entry.mem_length
			<< ") type: ";
		switch(entry.mem_type) {
		case 1: stream << "available memory"; break;
		case 3: stream << "available memory (holds ACPI information)"; break;
		case 4: stream << "reserved memory (preserve on hibernation)"; break;
		case 5: stream << "defective"; break;
		default: stream << "reserved memory"; break;
		}
		stream << "\n";
	}

	stream << "Shutting down\n";
}
