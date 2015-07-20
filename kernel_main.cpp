#include "hw/vga.hpp"
#include "hw/vga_stream.hpp"
#include "hw/multiboot.hpp"
#include "hw/segments.hpp"
#include "hw/interrupt_table.hpp"
#include "hw/interrupt.hpp"
#include "hw/cpu_io.hpp"
#include "oslibc/numeric.h"
#include "cloudos_version.h"

using namespace cloudos;

const char scancode_to_key[] = {
	0   , 0   , '1' , '2' , '3' , '4' , '5' , '6' , // 00-07
	'7' , '8' , '9' , '0' , '-' , '=' , '\b', '\t', // 08-0f
	'q' , 'w' , 'e' , 'r' , 't' , 'y' , 'u' , 'i' , // 10-17
	'o' , 'p' , '[' , ']' , '\n' , 0  , 'a' , 's' , // 18-1f
	'd' , 'f' , 'g' , 'h' , 'j' , 'k' , 'l' , ';' , // 20-27
	'\'', '`' , 0   , '\\', 'z' , 'x' , 'c' , 'v' , // 28-2f
	'b' , 'n' , 'm' , ',' , '.' , '/' , 0   , '*' , // 30-37
	0   , ' ' , 0   , 0   , 0   , 0   , 0   , 0   , // 38-3f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , '7' , // 40-47
	'8' , '9' , '-' , '4' , '5' , '6' , '+' , '1' , // 48-4f
	'2' , '3' , '0' , '.' // 50-53
};

struct interrupt_handler : public interrupt_functor {
	interrupt_handler(vga_stream *s) : stream(s) {}
	void operator()(interrupt_state_t *regs) {
		int int_no = regs->int_no;
		int err_code = regs->err_code;
		if(int_no == 0x20) {
			// timer interrupt
		} else if(int_no == 0x21) {
			// keyboard input!
			// wait for the ready bit to turn on
			uint32_t waits = 0;
			while((inb(0x64) & 0x1) == 0 && waits < 0xfffff) {
				waits++;
			}

			if((inb(0x64) & 0x1) == 0x1) {
				uint16_t scancode = inb(0x60);
				char buf[2];
				buf[0] = scancode_to_key[scancode];
				buf[1] = 0;
				*stream << buf;
			} else {
				*stream << "Waited for scancode for too long\n";
			}
		} else {
			*stream << "Got interrupt " << int_no << " (" << hex << int_no << dec << ", err code " << err_code << ")\n";
		}
	}
private:
	vga_stream *stream;
};

extern "C"
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

	// Set up segment table
	segment_table gdt;
	// first entry is always null
	gdt.add_entry(0, 0, 0, 0);
	// second entry: entire 4GiB address space is readable code
	gdt.add_entry(0xffffff, 0,
		SEGMENT_RW | SEGMENT_EXEC | SEGMENT_ALWAYS | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	// third entry: entire 4GiB address space is writable data
	gdt.add_entry(0xffffff, 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	gdt.load();
	stream << "Global Descriptor Table loaded, segmentation is in effect\n";

	interrupt_handler handler(&stream);

	interrupt_table interrupts;
	interrupt_global interrupts_global(&handler);
	interrupts_global.setup(interrupts);
	interrupts_global.reprogram_pic();

	stream << "Waiting for interrupts...\n";
	interrupts_global.enable_interrupts();
	while(1) {}
}
