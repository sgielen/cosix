#include "process_fd.hpp"
#include <oslibc/string.h>
#include <hw/vga_stream.hpp>
#include <memory/allocator.hpp>
#include <memory/page_allocator.hpp>
#include <global.hpp>

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

process_fd::process_fd(page_allocator *a, const char *n)
: fd_t(fd_type_t::process, n)
{
	page_allocation p;
	auto res = a->allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate process paging directory");
	}
	page_directory = reinterpret_cast<uint32_t*>(p.address);
	a->fill_kernel_pages(page_directory);

	res = a->allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate page table list");
	}
	page_tables = reinterpret_cast<uint32_t**>(p.address);
	for(size_t i = 0; i < 0x300; ++i) {
		page_tables[i] = nullptr;
	}
}

void cloudos::process_fd::initialize(void *start_addr, cloudos::allocator *alloc) {
	userland_stack_size = kernel_stack_size = 0x10000 /* 64 kb */;
	userland_stack_bottom = reinterpret_cast<uint8_t*>(alloc->allocate(userland_stack_size));
	kernel_stack_bottom   = reinterpret_cast<uint8_t*>(alloc->allocate(kernel_stack_size));

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;

	// stack location for new process
	uint32_t stack_address = 0x80000000;
	map_at(userland_stack_bottom, reinterpret_cast<void*>(stack_address - userland_stack_size), userland_stack_size);
	state.useresp = stack_address;

	// initial instruction pointer
	state.eip = reinterpret_cast<uint32_t>(start_addr);

	// allow interrupts
	const int INTERRUPT_ENABLE = 1 << 9;
	state.eflags = INTERRUPT_ENABLE;
}

void cloudos::process_fd::set_return_state(interrupt_state_t *new_state) {
	state = *new_state;
}

void cloudos::process_fd::get_return_state(interrupt_state_t *return_state) {
	*return_state = state;
}

void cloudos::process_fd::handle_syscall(vga_stream &stream) {
	// software interrupt
	int syscall = state.eax;
	if(syscall == 1) {
		// getpid
		state.eax = pid;
	} else if(syscall == 2) {
		// putstring
		const char *str = reinterpret_cast<const char*>(state.ecx);
		const size_t size = state.edx;
		state.edx = 0;
		for(size_t i = 0; i < size; ++i) {
			stream << str[i];
			state.edx += 1;
		}
	} else {
		stream << "Syscall " << state.eax << " unknown\n";
	}
}

void *cloudos::process_fd::get_kernel_stack_top() {
	return reinterpret_cast<char*>(kernel_stack_bottom) + kernel_stack_size;
}

uint32_t *process_fd::get_page_table(int i) {
	if(i >= 0x300) {
		kernel_panic("process_fd::get_page_table() cannot answer for kernel pages");
	}
	if(page_directory[i] & 0x1 /* present */) {
		return page_tables[i];
	} else {
		return nullptr;
	}
}

void process_fd::install_page_directory() {
	/* some sanity checks to warn early if the page directory looks incorrect */
	if(get_page_allocator()->to_physical_address(this, reinterpret_cast<void*>(0xc00b8000)) != reinterpret_cast<void*>(0xb8000)) {
		kernel_panic("Failed to map VGA page, VGA stream will fail later");
	}
	if(get_page_allocator()->to_physical_address(this, reinterpret_cast<void*>(0xc01031c6)) != reinterpret_cast<void*>(0x1031c6)) {
		kernel_panic("Kernel will fail to execute");
	}

#ifndef TESTING_ENABLED
	auto page_phys_address = get_page_allocator()->to_physical_address(&page_directory[0]);
	if((reinterpret_cast<uint32_t>(page_phys_address) & 0xfff) != 0) {
		kernel_panic("Physically allocated memory is not page-aligned");
	}
	// Set the paging directory in cr3
	asm volatile("mov %0, %%cr3" : : "a"(reinterpret_cast<uint32_t>(page_phys_address)) : "memory");

	// Turn on paging in cr0
	int cr0;
	asm volatile("mov %%cr0, %0" : "=a"(cr0));
	cr0 |= 0x80000000;
	asm volatile("mov %0, %%cr0" : : "a"(cr0) : "memory");
#endif
}

void process_fd::map_at(void *kernel_virt, void *userland_virt, size_t length)
{
	if(reinterpret_cast<uint32_t>(kernel_virt) < _kernel_virtual_base) {
		kernel_panic("Got non-kernel address in map_at()");
	}
	if(reinterpret_cast<uint32_t>(userland_virt) + length > _kernel_virtual_base) {
		kernel_panic("Got kernel address in map_at()");
	}
	if(reinterpret_cast<uint32_t>(kernel_virt) % PAGE_SIZE != 0) {
		kernel_panic("kernel_virt is not page aligned in map_at");
	}
	if(reinterpret_cast<uint32_t>(userland_virt) % PAGE_SIZE != 0) {
		kernel_panic("userland_virt is not page aligned in map_at");
	}

	void *phys_addr = get_page_allocator()->to_physical_address(kernel_virt);
	if(reinterpret_cast<uint32_t>(phys_addr) % PAGE_SIZE != 0) {
		kernel_panic("phys_addr is not page aligned in map_at");
	}

	uint16_t page_table_num = reinterpret_cast<uint64_t>(userland_virt) >> 22;
	if((page_directory[page_table_num] & 0x1) == 0) {
		// allocate page table
		page_allocation p;
		auto res = get_page_allocator()->allocate(&p);
		if(res != error_t::no_error) {
			kernel_panic("Failed to allocate kernel paging table in map_to");
		}

		auto address = get_page_allocator()->to_physical_address(p.address);
		if((reinterpret_cast<uint32_t>(address) & 0xfff) != 0) {
			kernel_panic("physically allocated memory is not page-aligned");
		}

		page_directory[page_table_num] = reinterpret_cast<uint64_t>(address) | 0x07 /* read-write userspace-accessible present table */;
		page_tables[page_table_num] = reinterpret_cast<uint32_t*>(p.address);
	}
	uint32_t *page_table = get_page_table(page_table_num);
	if(page_table == 0) {
		kernel_panic("Failed to map kernel paging table in map_to");
	}

	uint16_t page_entry_num = reinterpret_cast<uint64_t>(userland_virt) >> 12 & 0x03ff;
	uint32_t &page_entry = page_table[page_entry_num];
	if(page_entry & 0x1) {
		get_vga_stream() << "Page table " << page_table_num << ", page entry " << page_entry_num << " already mapped\n";
		get_vga_stream() << "Value: 0x" << hex << page_entry << dec << "\n";
		kernel_panic("Page in map_to already present");
	} else {
		page_entry = reinterpret_cast<uint32_t>(phys_addr) | 0x07; // read-write userspace-accessible present entry
	}

	if(length > PAGE_SIZE) {
		return map_at(reinterpret_cast<void*>(reinterpret_cast<uint32_t>(kernel_virt) + PAGE_SIZE),
			reinterpret_cast<void*>(reinterpret_cast<uint32_t>(userland_virt) + PAGE_SIZE),
			length - PAGE_SIZE);
	}
}
