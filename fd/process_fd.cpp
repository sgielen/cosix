#include "process_fd.hpp"
#include <oslibc/string.h>
#include <hw/vga_stream.hpp>
#include <memory/allocator.hpp>
#include <memory/page_allocator.hpp>
#include <global.hpp>
#include <fd/vga_fd.hpp>
#include <fd/memory_fd.hpp>
#include <fd/procfs.hpp>
#include <userland/vdso_support.h>
#include <cloudabi/headers/cloudabi_types.h>

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
	memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));
	a->fill_kernel_pages(page_directory);

	res = a->allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate page table list");
	}
	page_tables = reinterpret_cast<uint32_t**>(p.address);
	for(size_t i = 0; i < 0x300; ++i) {
		page_tables[i] = nullptr;
	}

	vga_fd *fd = get_allocator()->allocate<vga_fd>();
	new (fd) vga_fd("vga_fd");

	add_fd(fd);

	char *fd_buf = get_allocator()->allocate<char>(200);
	strncpy(fd_buf, "These are the contents of my buffer!\n", 200);

	memory_fd *fd2 = get_allocator()->allocate<memory_fd>();
	new (fd2) memory_fd(fd_buf, strlen(fd_buf) + 1, "memory_fd");
	add_fd(fd2);

	add_fd(procfs::get_root_fd());
}

int process_fd::add_fd(fd_t *fd) {
	if(last_fd >= MAX_FD - 1) {
		kernel_panic("fd's expired for process");
	}
	int fdnum = ++last_fd;
	fds[fdnum] = fd;
	return fdnum;
}

fd_t *process_fd::get_fd(int num) {
	if(num < 0 || num > last_fd || num >= MAX_FD) {
		return nullptr;
	}
	return fds[num];
}

void cloudos::process_fd::initialize(void *start_addr, cloudos::allocator *alloc) {
	userland_stack_size = kernel_stack_size = 0x10000 /* 64 kb */;
	userland_stack_bottom = reinterpret_cast<uint8_t*>(alloc->allocate_aligned(userland_stack_size, 4096));
	kernel_stack_bottom   = reinterpret_cast<uint8_t*>(alloc->allocate_aligned(kernel_stack_size, 4096));

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;

	// stack location for new process
	uint32_t stack_address = 0x80000000;
	map_at(userland_stack_bottom, reinterpret_cast<void*>(stack_address - userland_stack_size), userland_stack_size);
	state.useresp = stack_address;

	// initialize vdso address
	vdso_size = vdso_blob_size;
	vdso_image = alloc->allocate_aligned(vdso_size, 4096);
	memcpy(vdso_image, vdso_blob, vdso_size);
	uint32_t vdso_address = 0x80040000;
	map_at(vdso_image, reinterpret_cast<void*>(vdso_address), vdso_size);

	// initialize auxv
	size_t auxv_entries = 2; // including CLOUDABI_AT_NULL
	auxv_size = auxv_entries * sizeof(cloudabi_auxv_t);
	auxv_buf = alloc->allocate_aligned(auxv_size, 4096);
	cloudabi_auxv_t *auxv = reinterpret_cast<cloudabi_auxv_t*>(auxv_buf);
	auxv->a_type = CLOUDABI_AT_SYSINFO_EHDR;
	auxv->a_val = vdso_address;
	auxv++;
	auxv->a_type = CLOUDABI_AT_NULL;
	uint32_t auxv_address = 0x80010000;
	map_at(auxv_buf, reinterpret_cast<void*>(auxv_address), auxv_size);

	// put auxv on stack, so it looks like a function argument for _start
	state.useresp -= 2 * sizeof(void*);
	memcpy(reinterpret_cast<uint8_t*>(userland_stack_bottom) + userland_stack_size - sizeof(void*), &auxv_address, sizeof(void*));

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
		// getpid(), returns eax=pid
		state.eax = pid;
	} else if(syscall == 2) {
		// putstring(ebx=fd, ecx=ptr, edx=size), returns eax=0 or eax=-1 on error
		int fdnum = state.ebx;
		fd_t *global_fd = get_fd(fdnum);
		if(!global_fd) {
			get_vga_stream() << "fdnum " << fdnum << " is not a valid fd\n";
			state.eax = -1;
			return;
		}

		const char *str = reinterpret_cast<const char*>(state.ecx);
		const size_t size = state.edx;

		if(reinterpret_cast<uint32_t>(str) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(str) + size >= _kernel_virtual_base
		|| size >= 0x40000000
		|| get_page_allocator()->to_physical_address(this, reinterpret_cast<const void*>(str)) == nullptr) {
			get_vga_stream() << "putstring() of a non-userland-accessible string\n";
			state.eax = -1;
			return;
		}

		auto res = global_fd->putstring(str, size);
		state.eax = res == error_t::no_error ? 0 : -1;
	} else if(syscall == 3) {
		// getchar(ebx=fd, ecx=offset), returns eax=resultchar or eax=-1 on error
		int fdnum = state.ebx;
		fd_t *global_fd = get_fd(fdnum);
		if(!global_fd) {
			get_vga_stream() << "fdnum " << fdnum << " is not a valid fd\n";
			state.eax = -1;
			return;
		}

		size_t offset = state.ecx;
		char buf[1];

		size_t r = global_fd->read(offset, &buf[0], 1);
		if(r != 1 || global_fd->error != error_t::no_error) {
			state.eax = -1;
			return;
		}
		state.eax = buf[0];
	} else if(syscall == 4) {
		// openat(ebx=fd, ecx=pathname, edx=as_directory) returns eax=fd or eax=-1 on error
		int fdnum = state.edx;
		fd_t *global_fd = get_fd(fdnum);
		if(!global_fd) {
			get_vga_stream() << "fdnum " << fdnum << " is not a valid fd\n";
			state.eax = -1;
			return;
		}

		const char *pathname = reinterpret_cast<const char*>(state.ecx);
		int directory = state.ebx;

		fd_t *new_fd = global_fd->openat(pathname, directory == 1);
		if(!new_fd || global_fd->error != error_t::no_error) {
			get_vga_stream() << "failed to openat()\n";
			state.eax = -1;
			return;
		}

		int new_fdnum = add_fd(new_fd);
		state.eax = new_fdnum;
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
	while(true) {
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
			kernel_panic("Failed to map page table in map_to");
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

		if(length <= PAGE_SIZE) {
			break;
		}

		kernel_virt = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(kernel_virt) + PAGE_SIZE);
		userland_virt = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(userland_virt) + PAGE_SIZE);
		length -= PAGE_SIZE;
	}
}
