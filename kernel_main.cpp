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
#include "hw/net/intel_i217.hpp"
#include "hw/arch/x86/x86.hpp"
#include "net/loopback_interface.hpp"
#include "net/interface_store.hpp"
#include "oslibc/numeric.h"
#include "oslibc/string.h"
#include "cloudos_version.h"
#include "fd/process_fd.hpp"
#include "fd/scheduler.hpp"
#include "fd/bootfs.hpp"
#include "fd/initrdfs.hpp"
#include "fd/shmfs.hpp"
#include "memory/allocator.hpp"
#include "memory/page_allocator.hpp"
#include "memory/map_virtual.hpp"
#include "global.hpp"
#include "rng/rng.hpp"
#include <time/clock_store.hpp>
#include <fd/unixsock.hpp>
#include <term/terminal_store.hpp>
#include <term/console_terminal.hpp>
#include <blockdev/blockdev_store.hpp>
#include <proc/process_store.hpp>

using namespace cloudos;

cloudos::global_state *cloudos::global_state_;

uintptr_t __stack_chk_guard = 0;

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

	// Copy the cmdline to a buffer that will definitely remain valid
	char *orig_cmdline = boot_info.cmdline();
	auto cmdline_size = strlen(orig_cmdline) + 1;
	char cmdline_copy[cmdline_size];
	memcpy(cmdline_copy, orig_cmdline, cmdline_size);
	global.cmdline = cmdline_copy;
	stream << "Command line: " << global.cmdline << "\n";

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

		uint64_t begin_addr = entry.mem_base;
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

	// Load initrd
	// TODO: this code assumes initrd is loaded shortly after the kernel.
	// A bootloader is allowed to load it anywhere -- if it decides to load
	// it at end of memory, we won't have any physical pages to allocate.
	auto *module_base_address = boot_info.module_base_address();
	if(module_base_address && module_base_address->mmo_end > reinterpret_cast<uint32_t>(end_of_kernel)) {
		end_of_kernel = reinterpret_cast<void*>(module_base_address->mmo_end);
	}

	// end_of_kernel points at the end of any usable code, stack, BSS, initrd, etc
	// in physical memory, so everything after that is free for use by the
	// allocator
	page_allocator paging(end_of_kernel, mmap, memory_map_bytes);
	global.page_allocator = &paging;
	map_virtual vmap(&paging);
	global.map_virtual = &vmap;
	vmap.load_paging_stage2();

	initrdfs initrd(module_base_address);
	global.initrdfs = &initrd;

	allocator alloc_;
	global.alloc = &alloc_;

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

	interrupt_table interrupts;
	interrupt_handler int_handler;
	int_handler.setup(interrupts);
	int_handler.reprogram_pic();
	global.interrupt_handler = &int_handler;

	scheduler sched;
	global.scheduler = &sched;

	rng rng;
	// TODO: seed this properly, e.g. from the configfs, using the Intel
	// RDRAND instruction or by using the time in the hardware clock, and
	// only allow reading from it after it's been seeded
	rng.seed(98764);
	global.random = &rng;
	{
		uintptr_t stack_guard;
		rng.get(reinterpret_cast<char*>(&stack_guard), sizeof(stack_guard));
		__stack_chk_guard = stack_guard;
	}

	terminal_store term_store;
	global.terminal_store = &term_store;
	auto console = make_shared<console_terminal>();
	term_store.register_terminal(console);

	auto init = make_shared<process_fd>("init");
	global.init = init.get();
	stream << "Init process created\n";

	global.init->install_page_directory();
	stream << "Paging directory loaded, paging is in effect\n";
	vmap.free_paging_stage2();

	shmfs shared_memory_filesystem;
	global.shmfs = &shared_memory_filesystem;

	{
		auto bootfs_fd = bootfs::get_root_fd();
		if(!bootfs_fd) {
			kernel_panic("Failed to get bootfs fd");
		}
		auto init_exec_fd = bootfs_fd->openat("init", 4, 0, 0, nullptr);
		if(!init_exec_fd) {
			kernel_panic("Failed to open init");
		}
		char argdata[] = {0x06 /* empty ADT_MAP */};
		auto res = global.init->exec(init_exec_fd, 0, nullptr, argdata, sizeof(argdata));
		if(res != 0) {
			kernel_panic("Failed to start init");
		}
		global.init->add_initial_fds();
	}

	global.process_store = allocate<process_store>();
	global.process_store->register_process(init);

	global.clock_store = allocate<clock_store>();
	global.driver_store = allocate<driver_store>();
	global.blockdev_store = allocate<blockdev_store>();

#define REGISTER_DRIVER(TYPE) \
	do { \
		auto driver = allocate<TYPE>(); \
		get_driver_store()->register_driver(driver); \
	} while(0);

	REGISTER_DRIVER(x86_driver);
	REGISTER_DRIVER(pci_driver);
	REGISTER_DRIVER(virtio_net_driver);
	REGISTER_DRIVER(intel_i217_driver);

	global.interface_store = allocate<interface_store>();

	loopback_interface *loopback = allocate<loopback_interface>();
	if(global.interface_store->register_interface_fixed_name(loopback, "lo") != 0) {
		kernel_panic("Failed to register loopback interface");
	}

	global.root_device = allocate<root_device>();
	global.root_device->init();
	dump_device_descriptions(stream, global.root_device);

	dump_interfaces(stream, global.interface_store);

	stream << "Waiting for interrupts...\n";
	int_handler.enable_interrupts();

	// yield to init kernel thread
	get_scheduler()->initial_yield();
	kernel_panic("yield returned");
}

global_state::global_state() {
	memset(this, 0, sizeof(*this));
}
