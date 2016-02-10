#include "hw/vga.hpp"
#include "hw/vga_stream.hpp"
#include "hw/multiboot.hpp"
#include "hw/segments.hpp"
#include "hw/interrupt_table.hpp"
#include "hw/interrupt.hpp"
#include "hw/cpu_io.hpp"
#include "hw/root_device.hpp"
#include "hw/driver_store.hpp"
#include "hw/pci_bus.hpp"
#include "hw/net/virtio.hpp"
#include "net/loopback_interface.hpp"
#include "net/interface_store.hpp"
#include "net/udp.hpp"
#include "oslibc/numeric.h"
#include "oslibc/string.h"
#include "cloudos_version.h"
#include "userland/process.hpp"
#include "process/process.hpp"
#include "memory/allocator.hpp"
#include "global.hpp"

using namespace cloudos;

extern "C" char __end_of_binary;

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

static void send_udp_test_packet() {
	auto *eth0 = get_interface_store()->get_interface("eth0");
	if(!eth0) {
		return;
	}

	auto *list = eth0->get_ipv4addr_list();
	if(!list) {
		return;
	}

	ipv4addr_t source, destination;
	memcpy(source, list->data, 4);
	// 8.8.8.8
	destination[0] = destination[1] = destination[2] = destination[3] = 0x08;

	const char *payload = "Hello world!";
	error_t res = get_protocol_store()->udp->send_ipv4_udp(
		reinterpret_cast<const uint8_t*>(payload), strlen(payload), source, 53, destination, 53);
	if(res == error_t::no_error) {
		get_vga_stream() << "Sent a packet!\n";
	} else {
		get_vga_stream() << "Failed to send a packet: " << res << "\n";
	}
}

struct interrupt_handler : public interrupt_functor {
	interrupt_handler(global_state *g) : global(g), proc_ctr(0), int_first(true) {
		for(size_t i = 0; i < 3; ++i) {
			procs[i].initialize(i, reinterpret_cast<void*>(process_main), global->alloc);
		}
	}

	void operator()(interrupt_state_t *regs) {
		vga_stream *stream = global->vga;
		if(int_first) {
			// This boolean is only set to true for the first interrupt, because we only
			// switch to the first process on the first interrupt, and after that all
			// interrupts are from the userland.
			// TODO: Instead, we should detect whether an interrupt is coming from kernel
			// or userland, and what process, so we can make that difference and don't
			// need this boolean anymore.
			int_first = false;
		} else {
			procs[proc_ctr].set_return_state(regs);
		}

		int int_no = regs->int_no;
		int err_code = regs->err_code;
		if(int_no == 0x20) {
			get_root_device()->timer_event_recursive();
			// timer interrupt
			if(++proc_ctr == 3) {
				proc_ctr = 0;
			}
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
				if(buf[0] == 'u') {
					send_udp_test_packet();
				}
				buf[1] = 0;
				if(buf[0] == '\n') {
					*stream << hex << "Stack ptr: " << &scancode << "; returning to stack: " << regs->useresp << dec << "\n";
				} else if(scancode <= 0x53) {
					*stream << buf;
				}
			} else {
				*stream << "Waited for scancode for too long\n";
			}
		} else if(int_no == 0x80) {
			procs[proc_ctr].handle_syscall(*stream);
		} else {
			*stream << "Got interrupt " << int_no << " (0x" << hex << int_no << dec << ", err code " << err_code << ")\n";
			kernel_panic("Unknown interrupt received");
		}
		procs[proc_ctr].get_return_state(regs);
		global->gdt->set_kernel_stack(procs[proc_ctr].get_kernel_stack_top());
	}
private:
	global_state *global;
	cloudos::process procs[3];
	int proc_ctr;
	bool int_first;
};

cloudos::global_state *cloudos::global_state_;

extern "C"
void kernel_main(uint32_t multiboot_magic, void *bi_ptr) {
	global_state global;
	global_state_ = &global;
	vga_buffer buf;
	vga_stream stream(buf);
	global.vga = &stream;
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

	// __end_of_binary points at the end of any usable code, stack, BSS,
	// etc, so everything after that is free for use by the allocator
	allocator alloc_(reinterpret_cast<void*>(&__end_of_binary), mmap, memory_map_bytes);
	global.alloc = &alloc_;

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

		stream  << "  " << i << ": Addr 0x" << hex << begin_addr
			<< " to 0x" << end_addr << " (" << dec
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
		SEGMENT_RW | SEGMENT_EXEC | SEGMENT_ALWAYS | SEGMENT_PRIV_RING0 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	// third entry: entire 4GiB address space is writable data
	gdt.add_entry(0xffffff, 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRIV_RING0 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	// fourth entry: entire 4GiB address space is ring 3 readable code
	gdt.add_entry(0xfffff, 0,
		SEGMENT_RW | SEGMENT_EXEC | SEGMENT_ALWAYS | SEGMENT_PRIV_RING3 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT | SEGMENT_AVAILABLE);
	// fifth entry: entire 4GiB address space is ring 3 writable data
	gdt.add_entry(0xffffff, 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRIV_RING3 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT | SEGMENT_AVAILABLE);
	// and then a TSS entry
	gdt.add_tss_entry();
	gdt.load();
	global.gdt = &gdt;
	stream << "Global Descriptor Table loaded, segmentation is in effect\n";

	interrupt_handler handler(&global);

	interrupt_table interrupts;
	interrupt_global interrupts_global(&handler);
	interrupts_global.setup(interrupts);
	interrupts_global.reprogram_pic();

	global.driver_store = get_allocator()->allocate<driver_store>();
	new(global.driver_store) driver_store();

#define REGISTER_DRIVER(TYPE) \
	do { \
		auto driver = get_allocator()->allocate<TYPE>(); \
		new(driver) TYPE(); \
		get_driver_store()->register_driver(driver); \
	} while(0);

	REGISTER_DRIVER(pci_driver);
	REGISTER_DRIVER(virtio_net_driver);

	global.protocol_store = get_allocator()->allocate<protocol_store>();
	new(global.protocol_store) protocol_store();

	global.interface_store = get_allocator()->allocate<interface_store>();
	new(global.interface_store) interface_store();

	loopback_interface *loopback = get_allocator()->allocate<loopback_interface>();
	new(loopback) loopback_interface();
	if(global.interface_store->register_interface_fixed_name(loopback, "lo") != error_t::no_error) {
		kernel_panic("Failed to register loopback interface");
	}

	loopback->add_ipv4_addr(reinterpret_cast<const uint8_t*>("\x7f\x00\x00\x01"));

	global.root_device = get_allocator()->allocate<root_device>();
	new(global.root_device) root_device();
	global.root_device->init();
	dump_device_descriptions(stream, global.root_device);

	dump_interfaces(stream, global.interface_store);

	stream << "Waiting for interrupts...\n";
	interrupts_global.enable_interrupts();

	while(1) {}
}

global_state::global_state() {
	memset(this, 0, sizeof(*this));
}
