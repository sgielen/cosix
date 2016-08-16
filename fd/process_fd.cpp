#include "process_fd.hpp"
#include <oslibc/string.h>
#include <hw/vga_stream.hpp>
#include <net/elfrun.hpp> /* for elf_endian */
#include <memory/allocator.hpp>
#include <memory/page_allocator.hpp>
#include <global.hpp>
#include <fd/vga_fd.hpp>
#include <fd/memory_fd.hpp>
#include <fd/procfs.hpp>
#include <fd/bootfs.hpp>
#include <userland/vdso_support.h>
#include <elf.h>

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

process_fd::process_fd(const char *n)
: fd_t(CLOUDABI_FILETYPE_PROCESS, n)
{
	page_allocation p;
	auto res = get_page_allocator()->allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate process paging directory");
	}
	page_directory = reinterpret_cast<uint32_t*>(p.address);
	memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));
	get_page_allocator()->fill_kernel_pages(page_directory);

	res = get_page_allocator()->allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate page table list");
	}
	page_tables = reinterpret_cast<uint32_t**>(p.address);
	for(size_t i = 0; i < 0x300; ++i) {
		page_tables[i] = nullptr;
	}

	vga_fd *fd = get_allocator()->allocate<vga_fd>();
	new (fd) vga_fd("vga_fd");
	add_fd(fd, CLOUDABI_RIGHT_FD_WRITE);

	char *fd_buf = get_allocator()->allocate<char>(200);
	strncpy(fd_buf, "These are the contents of my buffer!\n", 200);

	memory_fd *fd2 = get_allocator()->allocate<memory_fd>();
	new (fd2) memory_fd(fd_buf, strlen(fd_buf) + 1, "memory_fd");
	add_fd(fd2, CLOUDABI_RIGHT_FD_READ);

	add_fd(procfs::get_root_fd(), CLOUDABI_RIGHT_FILE_OPEN, CLOUDABI_RIGHT_FD_READ | CLOUDABI_RIGHT_FILE_OPEN);

	add_fd(bootfs::get_root_fd(), CLOUDABI_RIGHT_FILE_OPEN, CLOUDABI_RIGHT_FD_READ | CLOUDABI_RIGHT_FILE_OPEN | CLOUDABI_RIGHT_PROC_EXEC);
}

int process_fd::add_fd(fd_t *fd, cloudabi_rights_t rights_base, cloudabi_rights_t rights_inheriting) {
	if(last_fd >= MAX_FD - 1) {
		// TODO: instead of keeping a last_fd counter, put mappings
		// into a freelist when they are closed, and allow them to be
		// reused. Then, return an error when there is no more free
		// space for fd's.
		kernel_panic("fd's expired for process");
	}
	fd_mapping_t *mapping = get_allocator()->allocate<fd_mapping_t>();
	mapping->fd = fd;
	mapping->rights_base = rights_base;
	mapping->rights_inheriting = rights_inheriting;

	int fdnum = ++last_fd;
	fds[fdnum] = mapping;
	return fdnum;
}

error_t process_fd::get_fd(fd_mapping_t **r_mapping, size_t num, cloudabi_rights_t has_rights) {
	*r_mapping = 0;
	if(num >= MAX_FD) {
		get_vga_stream() << "fdnum " << num << " is too high for an fd\n";
		return error_t::resource_exhausted;
	}
	fd_mapping_t *mapping = fds[num];
	if(mapping == 0 || mapping->fd == 0) {
		get_vga_stream() << "fdnum " << num << " is not a valid fd\n";
		return error_t::invalid_argument;
	}
	if((mapping->rights_base & has_rights) != has_rights) {
		get_vga_stream() << "get_fd: fd " << num << " has insufficient rights 0x" << hex << has_rights << dec << "\n";
		return error_t::not_capable;
	}
	*r_mapping = mapping;
	return error_t::no_error;
}

template <typename T>
static inline T *allocate_on_stack(uint32_t *&stack_addr, uint32_t &useresp) {
	stack_addr = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(stack_addr) - sizeof(T));
	useresp -= sizeof(T);
	return reinterpret_cast<T*>(stack_addr);
}

void cloudos::process_fd::initialize(void *start_addr) {
	userland_stack_size = kernel_stack_size = 0x10000 /* 64 kb */;
	userland_stack_bottom = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(userland_stack_size, PAGE_SIZE));
	kernel_stack_bottom   = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(kernel_stack_size, PAGE_SIZE));

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;
	// set fsbase
	state.fs = 0x33;

	// stack location for new process
	uint32_t *stack_addr = reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(userland_stack_bottom) + userland_stack_size);
	userland_stack_address = reinterpret_cast<void*>(0x80000000);
	map_at(userland_stack_bottom, reinterpret_cast<void*>(reinterpret_cast<uint32_t>(userland_stack_address) - userland_stack_size), userland_stack_size);
	state.useresp = reinterpret_cast<uint32_t>(userland_stack_address);

	// initialize vdso address
	vdso_size = vdso_blob_size;
	vdso_image = get_allocator()->allocate_aligned(vdso_size, PAGE_SIZE);
	memcpy(vdso_image, vdso_blob, vdso_size);
	uint32_t vdso_address = 0x80040000;
	map_at(vdso_image, reinterpret_cast<void*>(vdso_address), vdso_size);

	// initialize elf phdr address
	uint32_t elf_phdr_address = 0x80060000;
	map_at(elf_phdr, reinterpret_cast<void*>(elf_phdr_address), elf_ph_size);

	// initialize auxv
	if(elf_ph_size == 0 || elf_phdr == 0) {
		kernel_panic("About to start process but no elf_phdr present");
	}
	size_t auxv_entries = 6; // including CLOUDABI_AT_NULL
	auxv_size = auxv_entries * sizeof(cloudabi_auxv_t);
	auxv_buf = get_allocator()->allocate_aligned(auxv_size, PAGE_SIZE);
	cloudabi_auxv_t *auxv = reinterpret_cast<cloudabi_auxv_t*>(auxv_buf);
	auxv->a_type = CLOUDABI_AT_BASE;
	auxv->a_ptr = nullptr; /* because we don't do address randomization */
	auxv++;
	auxv->a_type = CLOUDABI_AT_PAGESZ;
	auxv->a_val = PAGE_SIZE;
	auxv++;
	auxv->a_type = CLOUDABI_AT_SYSINFO_EHDR;
	auxv->a_val = vdso_address;
	auxv++;
	auxv->a_type = CLOUDABI_AT_PHDR;
	auxv->a_val = elf_phdr_address;
	auxv++;
	auxv->a_type = CLOUDABI_AT_PHNUM;
	auxv->a_val = elf_phnum;
	auxv++;
	auxv->a_type = CLOUDABI_AT_NULL;
	uint32_t auxv_address = 0x80010000;
	map_at(auxv_buf, reinterpret_cast<void*>(auxv_address), auxv_size);

	// memory for the TCB pointer and area
	void **tcb_address = allocate_on_stack<void*>(stack_addr, state.useresp);
	cloudabi_tcb_t *tcb = allocate_on_stack<cloudabi_tcb_t>(stack_addr, state.useresp);
	*tcb_address = reinterpret_cast<void*>(state.useresp);
	// we don't currently use the TCB pointer, so set it to zero
	memset(tcb, 0, sizeof(*tcb));

	// initialize stack so that it looks like _start(auxv_address) is called
	*allocate_on_stack<void*>(stack_addr, state.useresp) = reinterpret_cast<void*>(auxv_address);
	*allocate_on_stack<void*>(stack_addr, state.useresp) = 0;

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

	// TODO: for all system calls: check if all pointers refer to valid
	// memory areas and if userspace has access to all of them

	int syscall = state.eax;
	if(syscall == 1) {
		// getpid(), returns eax=pid
		state.eax = pid;
	} else if(syscall == 2) {
		// putstring(ebx=fd, ecx=ptr, edx=size), returns eax=0 or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE);
		if(res != error_t::no_error) {
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

		res = mapping->fd->putstring(str, size);
		state.eax = res == error_t::no_error ? 0 : -1;
	} else if(syscall == 3) {
		// getchar(ebx=fd, ecx=offset), returns eax=resultchar or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		size_t offset = state.ecx;
		char buf[1];

		size_t r = mapping->fd->read(offset, &buf[0], 1);
		if(r != 1 || mapping->fd->error != error_t::no_error) {
			state.eax = -1;
			return;
		}
		state.eax = buf[0];
	} else if(syscall == 4) {
		// sys_proc_file_open(ecx=parameters) returns eax=fd or eax=-1 on error
		struct args_t {
			cloudabi_lookup_t dirfd;
			const char *path;
			size_t pathlen;
			cloudabi_oflags_t oflags;
			const cloudabi_fdstat_t *fds;
			cloudabi_fd_t *fd;
		};
		args_t *args = reinterpret_cast<args_t*>(state.ecx);

		int fdnum = args->dirfd.fd;
		fd_mapping_t *mapping;
		auto res = get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_OPEN);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		// TODO: take lookup flags into account, args->dirfd.flags

		// check if fd can be created with such rights
		if((mapping->rights_inheriting & args->fds->fs_rights_base) != args->fds->fs_rights_base
		|| (mapping->rights_inheriting & args->fds->fs_rights_inheriting) != args->fds->fs_rights_inheriting) {
			get_vga_stream() << "userspace wants too many permissions\n";
			state.eax = -1;
		}

		fd_t *new_fd = mapping->fd->openat(args->path, args->pathlen, args->oflags, args->fds);
		if(!new_fd || mapping->fd->error != error_t::no_error) {
			get_vga_stream() << "failed to openat()\n";
			state.eax = -1;
			return;
		}

		int new_fdnum = add_fd(new_fd, args->fds->fs_rights_base, args->fds->fs_rights_inheriting);
		*(args->fd) = new_fdnum;
		state.eax = 0;
	} else if(syscall == 5) {
		// sys_fd_stat_get(ebx=fd, ecx=fdstat_t) returns eax=fd or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = get_fd(&mapping, fdnum, 0);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		cloudabi_fdstat_t *stat = reinterpret_cast<cloudabi_fdstat_t*>(state.ecx);

		// TODO: check if ecx until ecx+sizeof(fdstat_t) is valid *writable* process memory
		if(reinterpret_cast<uint32_t>(stat) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(stat) + sizeof(cloudabi_fdstat_t) >= _kernel_virtual_base
		|| get_page_allocator()->to_physical_address(this, reinterpret_cast<const void*>(stat)) == nullptr) {
			get_vga_stream() << "sys_fd_stat_get() of a non-userland-accessible string\n";
			state.eax = -1;
			return;
		}

		stat->fs_filetype = mapping->fd->type;
		stat->fs_flags = mapping->fd->flags;
		stat->fs_rights_base = mapping->rights_base;
		stat->fs_rights_inheriting = mapping->rights_inheriting;
		state.eax = 0;
	} else if(syscall == 6) {
		// sys_fd_proc_exec(ecx=parameters) returns eax=-1 on error
		struct args_t {
			cloudabi_fd_t fd;
			const void *data;
			size_t datalen;
			const cloudabi_fd_t *fds;
			size_t fdslen;
		};

		args_t *args = reinterpret_cast<args_t*>(state.ecx);

		fd_mapping_t *mapping;
		auto res = get_fd(&mapping, args->fd, CLOUDABI_RIGHT_PROC_EXEC);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		fd_mapping_t *new_fds[args->fdslen];
		for(size_t i = 0; i < args->fdslen; ++i) {
			fd_mapping_t *old_mapping;
			res = get_fd(&old_mapping, args->fds[i], 0);
			if(res != error_t::no_error) {
				// request to map an invalid fd
				state.eax = -1;
				return;
			}
			// copy the mapping to the new process
			new_fds[i] = old_mapping;
		}

		uint32_t *old_page_directory = page_directory;
		uint32_t **old_page_tables = page_tables;

		page_allocation p;
		res = get_page_allocator()->allocate(&p);
		if(res != error_t::no_error) {
			kernel_panic("Failed to allocate process paging directory");
		}
		page_directory = reinterpret_cast<uint32_t*>(p.address);
		memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));

		get_page_allocator()->fill_kernel_pages(page_directory);
		res = get_page_allocator()->allocate(&p);
		if(res != error_t::no_error) {
			kernel_panic("Failed to allocate page table list");
		}
		page_tables = reinterpret_cast<uint32_t**>(p.address);
		for(size_t i = 0; i < 0x300; ++i) {
			page_tables[i] = nullptr;
		}

		res = exec(mapping->fd);
		if(res != error_t::no_error) {
			get_vga_stream() << "exec() failed because of " << res << "\n";
			page_directory = old_page_directory;
			page_tables = old_page_tables;
			state.eax = -1;
			return;
		}

		// Close all unused FDs
		for(int i = 0; i <= last_fd; ++i) {
			bool fd_is_used = false;
			for(size_t j = 0; j < args->fdslen; ++j) {
				// TODO: cloudabi does not allow an fd to be mapped twice in exec()
				if(new_fds[j]->fd == fds[i]->fd) {
					fd_is_used = true;
				}
			}
			if(!fd_is_used) {
				// TODO: actually close
				fds[i]->fd = nullptr;
			}
		}

		for(size_t i = 0; i < args->fdslen; ++i) {
			fds[i] = new_fds[i];
		}
		last_fd = args->fdslen - 1;

		// TODO this is a hack, remove this -- always add the VGA stream as an fd
		vga_fd *fd = get_allocator()->allocate<vga_fd>();
		new (fd) vga_fd("vga_fd");
		add_fd(fd, CLOUDABI_RIGHT_FD_WRITE);

		// now, when process is scheduled again, we will return to the entrypoint of the new binary
	} else {
		stream << "Syscall " << state.eax << " unknown\n";
	}
}

void *cloudos::process_fd::get_kernel_stack_top() {
	return reinterpret_cast<char*>(kernel_stack_bottom) + kernel_stack_size;
}

void *cloudos::process_fd::get_fsbase() {
	return reinterpret_cast<void*>(reinterpret_cast<uint32_t>(userland_stack_address) - sizeof(void*));
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

error_t process_fd::exec(fd_t *fd) {
	// read from this fd until it gives EOF, then exec(buf, buf_size)
	// TODO: once all fds implement seek(), we can read() only the header,
	// then seek to the phdr offset, then read() phdrs, then for every LOAD
	// phdr, seek() to the binary data and read() only that into the
	// process address space
	uint8_t *elf_buffer = get_allocator()->allocate<uint8_t>(10 * 1024 * 1024);
	size_t buffer_size = 0;
	do {
		size_t read = fd->read(buffer_size, &elf_buffer[buffer_size], 1024);
		buffer_size += read;
		if(read == 0) {
			break;
		}
	} while(fd->error == error_t::no_error);

	if(fd->error != error_t::no_error) {
		return fd->error;
	}

	return exec(elf_buffer, buffer_size);
}

error_t process_fd::exec(uint8_t *buffer, size_t buffer_size) {
	if(buffer_size < sizeof(Elf32_Ehdr)) {
		// Binary too small
		return error_t::exec_format;
	}

	Elf32_Ehdr *header = reinterpret_cast<Elf32_Ehdr*>(buffer);
	if(memcmp(header->e_ident, "\x7F" "ELF", 4) != 0) {
		// Not an ELF binary
		return error_t::exec_format;
	}

	if(header->e_ident[EI_CLASS] != ELFCLASS32) {
		// Not a 32-bit ELF binary
		return error_t::exec_format;
	}

	if(header->e_ident[EI_DATA] != ELFDATA2LSB) {
		// Not least-significant byte first, unsupported at the moment
		return error_t::exec_format;
	}

	if(header->e_ident[EI_VERSION] != 1) {
		// Not ELF version 1
		return error_t::exec_format;
	}

	if(header->e_ident[EI_OSABI] != ELFOSABI_CLOUDABI
	|| header->e_ident[EI_ABIVERSION] != 0) {
		// Not CloudABI v0
		return error_t::exec_format;
	}

	if(header->e_type != ET_EXEC && header->e_type != ET_DYN) {
		// Not an executable or shared object file
		// (CloudABI binaries can be shipped as shared object files,
		// which are actually executables, so that the kernel knows it
		// can map them anywhere in address space for ASLR)
		return error_t::exec_format;
	}

	if(header->e_machine != EM_386) {
		// TODO: when we support different machine types, check that
		// header->e_machine is supported.
		return error_t::exec_format;
	}

	if(header->e_version != EV_CURRENT) {
		// Not a current version ELF
		return error_t::exec_format;
	}

	// Save the phdrs
	elf_phnum = header->e_phnum;
	elf_ph_size = header->e_phentsize * elf_phnum;

	if(header->e_phoff >= buffer_size || (header->e_phoff + elf_ph_size) >= buffer_size) {
		// Phdrs weren't shipped in this ELF
		return error_t::exec_format;
	}

	elf_phdr = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(elf_ph_size, 4096));
	memcpy(elf_phdr, buffer + header->e_phoff, elf_ph_size);

	// Map the LOAD sections
	for(size_t phi = 0; phi < elf_phnum; ++phi) {
		size_t offset = header->e_phoff + phi * header->e_phentsize;
		if(offset >= buffer_size || (offset + sizeof(Elf32_Phdr)) >= buffer_size) {
			// Phdr wasn't shipped in this ELF
			return error_t::exec_format;
		}

		Elf32_Phdr *phdr = reinterpret_cast<Elf32_Phdr*>(buffer + offset);

		if(phdr->p_type == PT_LOAD) {
			if(phdr->p_offset >= buffer_size || (phdr->p_offset + phdr->p_filesz) >= buffer_size) {
				// Phdr data wasn't shipped in this ELF
				return error_t::exec_format;
			}
			uint8_t *code_offset = buffer + phdr->p_offset;
			uint8_t *codebuf = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(phdr->p_memsz, PAGE_SIZE));
			memcpy(codebuf, code_offset, phdr->p_filesz);
			memset(codebuf + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
			map_at(codebuf, reinterpret_cast<void*>(phdr->p_vaddr), phdr->p_memsz);
		}
	}

	// Initialize the process
	initialize(reinterpret_cast<void*>(header->e_entry));
	return error_t::no_error;
}

void process_fd::save_sse_state() {
	asm volatile("fxsave %0" : "=m" (sse_state));
}

void process_fd::restore_sse_state() {
	asm volatile("fxrstor %0" : "=m" (sse_state));
}
