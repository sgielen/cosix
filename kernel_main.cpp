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
#include "hw/sse.hpp"
#include "hw/net/virtio.hpp"
#include "net/loopback_interface.hpp"
#include "net/interface_store.hpp"
#include "net/udp.hpp"
#include "oslibc/numeric.h"
#include "oslibc/string.h"
#include "cloudos_version.h"
#include "fd/process_fd.hpp"
#include "fd/scheduler.hpp"
#include "fd/bootfs.hpp"
#include "memory/allocator.hpp"
#include "memory/page_allocator.hpp"
#include "global.hpp"
#include "rng/rng.hpp"

extern uint32_t _kernel_virtual_base;

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

static void request_process_binary() {
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
	// 192.168.178.27
	destination[0] = 192;
	destination[1] = 168;
	destination[2] = 178;
	destination[3] = 27;
	// 192.168.0.133
	/*destination[0] = 192;
	destination[1] = 168;
	destination[2] = 0;
	destination[3] = 133;*/
	// 192.168.2.106
	/*destination[0] = 192;
	destination[1] = 168;
	destination[2] = 2;
	destination[3] = 106;*/
	const char *payload = "process.bin";
	error_t res = get_protocol_store()->udp->send_ipv4_udp(
		reinterpret_cast<const uint8_t*>(payload), strlen(payload), source, 4445, destination, 4444);
	if(res == error_t::no_error) {
		get_vga_stream() << "Requested a binary!\n";
	} else {
		get_vga_stream() << "Failed to send a packet: " << res << "\n";
	}
}

const char *int_num_to_name(int int_no, bool *err_code) {
	bool errcode = false;
	const char *str = nullptr;
	switch(int_no) {
	case 0:  str = "Divide-by-zero (#DE)"; break;
	case 1:  str = "Debug (#DB)"; break;
	case 2:  str = "Non-maskable interrupt"; break;
	case 3:  str = "Breakpoint (#BP)"; break;
	case 4:  str = "Overflow (#OF)"; break;
	case 5:  str = "Bound Range Exceeded (#BR)"; break;
	case 6:  str = "Invalid Opcode (#UD)"; break;
	case 7:  str = "Device Not Available (#NM)"; break;
	case 8:  str = "Double Fault (#DF)"; break;
	case 9:  str = "Coprocessor Segment Overrun"; break;
	case 10: str = "Invalid TSS (#TS)"; errcode = true; break;
	case 11: str = "Segment Not Present (#NP)"; errcode = true; break;
	case 12: str = "Stack-Segment Fault (#SS)"; errcode = true; break;
	case 13: str = "General Protection Fault (#GP)"; errcode = true; break;
	case 14: str = "Page Fault (#PF)"; errcode = true; break;

	case 16: str = "x87 Floating-Point Exception (#MF)"; break;
	case 17: str = "Alignment Check (#AC)"; errcode = true; break;
	case 18: str = "Machine Check (#MC)"; break;
	case 19: str = "SIMD Floating-Point Exception (#XM/#XF)"; break;
	case 20: str = "Virtualization Exception (#VE)"; break;

	case 30: str = "Security Exception (#SX)"; errcode = true; break;

	default: str = "Unknown exception"; break;
	}
	if(err_code) *err_code = errcode;
	return str;
}

__attribute__((noreturn)) static void fatal_exception(int int_no, int err_code, interrupt_state_t *regs) {
	auto &stream = get_vga_stream();
	stream << "\n\n=============================================\n";
	stream << "Fatal exception during processing in kernel\n";
	bool errcode;
	stream << "Interrupt number: " << int_no << " - " << int_num_to_name(int_no, &errcode) << "\n";
	if(errcode) {
		stream << "Error Code: 0x" << hex << err_code << dec << "\n";
	}

	thread *thread = get_scheduler()->get_running_thread();
	if(thread) {
		stream << "Active process: " << thread->get_process() << " (\"" << thread->get_process()->name << "\")\n";
	}

	stream << "Instruction pointer at point of fault: 0x" << hex << regs->eip << dec << "\n";

	if(int_no == 0x0e /* Page fault */) {
		if(err_code & 0x01) {
			stream << "Caused by a page-protection violation during page ";
		} else {
			stream << "Caused by a non-present page during page ";
		}
		stream << ((err_code & 0x02) ? "write" : "read");
		stream << ((err_code & 0x04) ? " in unprivileged mode" : " in kernel mode");
		if(err_code & 0x08) {
			stream << " as a result of reading a reserved field";
		}
		if(err_code & 0x10) {
			stream << " as a result of an instruction fetch";
		}
		stream << "\n";
		uint32_t address;
		asm volatile("mov %%cr2, %0" : "=a"(address));
		stream << "Virtual address accessed: 0x" << hex << address << dec << "\n";
	}

	stream << "\n";
	kernel_panic("A fatal exception occurred.");
}

struct interrupt_handler : public interrupt_functor {
	void operator()(interrupt_state_t *regs) {
		int int_no = regs->int_no;
		int err_code = regs->err_code;

		if(regs->cs != 27 && regs->cs != 8) {
			get_vga_stream() << "!!!! Interrupt occurred, but unexpected code segment value !!!!\n";
			fatal_exception(int_no, err_code, regs);
		}

		bool in_kernel = regs->cs == 8;
		auto running_thread = get_scheduler()->get_running_thread();
		if(running_thread) {
			running_thread->set_return_state(regs);
		}

		if(!in_kernel && !running_thread) {
			get_vga_stream() << "!!!! Interrupt occurred in userland, but without an active thread !!!!\n";
			fatal_exception(int_no, err_code, regs);
		}

		// TODO: handle page fault as a special case, because it can be
		// solved by the running_thread

		// Any exceptions in the userland are handled by the thread
		if(!in_kernel && (int_no < 0x20 || int_no >= 0x30)) {
			running_thread->interrupt(int_no, err_code);
		}
		// Any exceptions in the kernel lead to immediate kernel_panic
		else if(int_no < 0x20 || int_no >= 0x30) {
			fatal_exception(int_no, err_code, regs);
		}
		// Hardware interrupts are handled normally
		else {
			int irq = int_no - 0x20;
			if(irq == 0 /* system timer */) {
				get_root_device()->timer_event_recursive();
				get_scheduler()->thread_yield();
			} else if(irq == 1 /* keyboard */) {
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
					} else if(buf[0] == 'p') {
						request_process_binary();
					}
					buf[1] = 0;
					if(buf[0] == '\n') {
						get_vga_stream() << hex << "Stack ptr: " << &scancode << "; returning to stack: " << regs->useresp << dec << "\n";
					} else if(scancode <= 0x53) {
						get_vga_stream() << buf;
					}
				} else {
					get_vga_stream() << "Waited for scancode for too long\n";
				}
			} else {
				get_vga_stream() << "Got unknown hardware interrupt " << irq << "\n";
			}
		}

		if(running_thread) {
			running_thread->get_return_state(regs);
		}
	}
};

cloudos::global_state *cloudos::global_state_;

extern "C"
void kernel_main(uint32_t multiboot_magic, void *bi_ptr, void *end_of_kernel) {
	global_state global;
	global_state_ = &global;
	vga_buffer buf;
	vga_stream stream(buf);
	global.vga = &stream;
	stream << "CloudOS v" cloudos_VERSION " -- starting up\n";
	char cpu_name[13];
	get_cpu_name(cpu_name);
	stream << "Running on CPU: " << cpu_name << "\n";

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

	stream << "Memory map as provided in Multiboot information:\n";
	iterate_through_mem_map(mmap, memory_map_bytes, [&](memory_map_entry *e) {
		memory_map_entry &entry = *e;

		uint64_t begin_addr = reinterpret_cast<uint64_t>(entry.mem_base.addr);
		uint64_t end_addr = begin_addr + entry.mem_length;

		stream  << "* Addr 0x" << hex << begin_addr
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
	});

	sse_enable();

	allocator alloc_;
	global.alloc = &alloc_;

	// end_of_kernel points at the end of any usable code, stack, BSS, etc
	// in physical memory, so everything after that is free for use by the
	// allocator
	page_allocator paging(end_of_kernel, mmap, memory_map_bytes);
	global.page_allocator = &paging;

	// Set up segment table
	segment_table gdt;
	// first entry is always null
	gdt.add_entry(0, 0, 0, 0);
	// second entry: entire 4GiB address space is readable code
	gdt.add_entry(0xffffffff, 0,
		SEGMENT_RW | SEGMENT_EXEC | SEGMENT_ALWAYS | SEGMENT_PRIV_RING0 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	// third entry: entire 4GiB address space is writable data
	gdt.add_entry(0xffffffff, 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRIV_RING0 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT);
	// fourth entry: entire 4GiB address space is ring 3 readable code
	gdt.add_entry(0xffffffff, 0,
		SEGMENT_RW | SEGMENT_EXEC | SEGMENT_ALWAYS | SEGMENT_PRIV_RING3 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT | SEGMENT_AVAILABLE);
	// fifth entry: entire 4GiB address space is ring 3 writable data
	gdt.add_entry(0xffffffff, 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRIV_RING3 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT | SEGMENT_AVAILABLE);
	// and then a TSS entry
	gdt.add_tss_entry();
	// lastly, add an entry for the %fs segment, used for thread-local storage
	gdt.add_fs_entry();
	gdt.load();
	global.gdt = &gdt;
	stream << "Global Descriptor Table loaded, segmentation is in effect\n";

	scheduler sched;
	global.scheduler = &sched;

	interrupt_handler handler;

	rng rng;
	rng.seed(98764);
	global.random = &rng;

	global.init = get_allocator()->allocate<process_fd>();
	new (global.init) process_fd("init");
	stream << "Init process created\n";

	global.init->install_page_directory();
	stream << "Paging directory loaded, paging is in effect\n";

	fd_t *bootfs_fd = bootfs::get_root_fd();
	fd_t *init_exec_fd = bootfs_fd->openat("init", 4, 0, 0);
	if(init_exec_fd == nullptr) {
		kernel_panic("Failed to open init");
	}
	auto res = global.init->exec(init_exec_fd, 0, nullptr, nullptr, 0);
	if(res != 0) {
		kernel_panic("Failed to start init");
	}
	global.init->add_initial_fds();

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

	// yield to init kernel thread
	get_scheduler()->initial_yield();
	kernel_panic("yield returned");
}

global_state::global_state() {
	memset(this, 0, sizeof(*this));
}
