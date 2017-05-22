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
#include "memory/allocator.hpp"
#include "memory/page_allocator.hpp"
#include "memory/map_virtual.hpp"
#include "global.hpp"
#include "rng/rng.hpp"
#include <time/clock_store.hpp>
#include <fd/unixsock.hpp>

using namespace cloudos;

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

	// end_of_kernel points at the end of any usable code, stack, BSS, etc
	// in physical memory, so everything after that is free for use by the
	// allocator
	page_allocator paging(end_of_kernel, mmap, memory_map_bytes);
	global.page_allocator = &paging;
	map_virtual vmap(&paging);
	global.map_virtual = &vmap;
	vmap.load_paging_stage2();

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

	scheduler sched;
	global.scheduler = &sched;

	rng rng;
	rng.seed(98764);
	global.random = &rng;

	global.init = get_allocator()->allocate<process_fd>();
	new (global.init) process_fd("init");
	stream << "Init process created\n";

	global.init->install_page_directory();
	stream << "Paging directory loaded, paging is in effect\n";
	vmap.free_paging_stage2();

	{
		auto bootfs_fd = bootfs::get_root_fd();
		if(!bootfs_fd) {
			kernel_panic("Failed to get bootfs fd");
		}
		auto init_exec_fd = bootfs_fd->openat("init", 4, 0, 0);
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

	interrupt_table interrupts;
	interrupt_handler int_handler;
	int_handler.setup(interrupts);
	int_handler.reprogram_pic();
	global.interrupt_handler = &int_handler;

	global.clock_store = allocate<clock_store>();

	global.driver_store = get_allocator()->allocate<driver_store>();
	new(global.driver_store) driver_store();

#define REGISTER_DRIVER(TYPE) \
	do { \
		auto driver = get_allocator()->allocate<TYPE>(); \
		new(driver) TYPE(); \
		get_driver_store()->register_driver(driver); \
	} while(0);

	REGISTER_DRIVER(x86_driver);
	REGISTER_DRIVER(pci_driver);
	REGISTER_DRIVER(virtio_net_driver);
	REGISTER_DRIVER(intel_i217_driver);

	global.interface_store = get_allocator()->allocate<interface_store>();
	new(global.interface_store) interface_store();

	loopback_interface *loopback = get_allocator()->allocate<loopback_interface>();
	new(loopback) loopback_interface();
	if(global.interface_store->register_interface_fixed_name(loopback, "lo") != 0) {
		kernel_panic("Failed to register loopback interface");
	}

	global.root_device = get_allocator()->allocate<root_device>();
	new(global.root_device) root_device();
	global.root_device->init();
	dump_device_descriptions(stream, global.root_device);

	dump_interfaces(stream, global.interface_store);

	unixsock_listen_store uls;
	global.unixsock_listen_store = &uls;

	stream << "Waiting for interrupts...\n";
	int_handler.enable_interrupts();

	// yield to init kernel thread
	get_scheduler()->initial_yield();
	kernel_panic("yield returned");
}

global_state::global_state() {
	memset(this, 0, sizeof(*this));
}
