#include <concur/cv.hpp>
#include <elf.h>
#include <fd/bootfs.hpp>
#include <fd/ifstoresock.hpp>
#include <fd/initrdfs.hpp>
#include <fd/memory_fd.hpp>
#include <fd/process_fd.hpp>
#include <fd/procfs.hpp>
#include <fd/pseudo_fd.hpp>
#include <fd/scheduler.hpp>
#include <fd/unixsock.hpp>
#include <fd/vga_fd.hpp>
#include <global.hpp>
#include <hw/vga_stream.hpp>
#include <memory/map_virtual.hpp>
#include <oslibc/string.h>
#include <rng/rng.hpp>
#include <term/terminal_store.hpp>
#include <userland/vdso_support.h>

using namespace cloudos;

extern uint32_t _kernel_virtual_base;

static bool process_already_terminated(void *userdata, thread_condition*) {
	return reinterpret_cast<process_fd*>(userdata)->is_terminated();
}

process_fd::process_fd(const char *n)
: fd_t(CLOUDABI_FILETYPE_PROCESS, 0, n)
{
	Blk page_directory_alloc = allocate_aligned(PAGE_SIZE, PAGE_SIZE);
	if(page_directory_alloc.ptr == nullptr) {
		kernel_panic("Couldn't allocate page directory for new process");
	}
	page_directory = reinterpret_cast<uint32_t*>(page_directory_alloc.ptr);
	memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));

	get_map_virtual()->fill_kernel_pages(page_directory);

	Blk page_tables_alloc = allocate(0x300 * sizeof(uint32_t*));
	if(page_tables_alloc.ptr == nullptr) {
		kernel_panic("Couldn't allocate page tables list for new process");
	}
	page_tables = reinterpret_cast<uint32_t**>(page_tables_alloc.ptr);
	for(size_t i = 0; i < 0x300; ++i) {
		page_tables[i] = nullptr;
	}

	termination_signaler.set_already_satisfied_function(process_already_terminated, this);
}

process_fd::~process_fd()
{
	if(running) {
		// immediately exit
		exit(0, CLOUDABI_SIGKILL);
	}
	assert(!running);

	assert(threads == nullptr);

	if(fds != nullptr) {
		deallocate({fds, fd_capacity * sizeof(fd_mapping_t*)});
		fds = nullptr;
		fd_capacity = 0;
	}

	remove_all(&mappings, [&](mem_mapping_list *) {
		return true;
	}, [&](mem_mapping_list *item) {
		item->data->unmap_completely();
		deallocate(item->data);
		deallocate(item);
	});

	deallocate({page_directory, PAGE_SIZE});
	for(size_t i = 0; i < 0x300; ++i) {
		if(page_tables[i] != nullptr) {
			deallocate({page_tables[i], PAGE_SIZE});
		}
	}
	deallocate({page_tables, 0x300 * sizeof(uint32_t*)});
}

void process_fd::add_initial_fds() {
	auto vga = make_shared<vga_fd>("vga_fd");
	add_fd(vga, CLOUDABI_RIGHT_FD_WRITE | CLOUDABI_RIGHT_FILE_STAT_FGET);

	Blk fd_buf = allocate(200);
	strncpy(reinterpret_cast<char*>(fd_buf.ptr), "These are the contents of my buffer!\n", fd_buf.size);

	auto memory = make_shared<memory_fd>(fd_buf, strlen(reinterpret_cast<char*>(fd_buf.ptr)) + 1, "memory_fd");
	add_fd(memory, CLOUDABI_RIGHT_FD_READ);

	add_fd(procfs::get_root_fd(),
		// base rights
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_STAT_FGET,
		// inherited rights
		CLOUDABI_RIGHT_FD_READ |
		CLOUDABI_RIGHT_FD_WRITE |
		CLOUDABI_RIGHT_FD_SEEK |
		CLOUDABI_RIGHT_FD_TELL |
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_STAT_FGET |
		CLOUDABI_RIGHT_FILE_STAT_GET
	);

	add_fd(bootfs::get_root_fd(),
		// base rights
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_STAT_FGET,
		// inherited rights
		CLOUDABI_RIGHT_FD_READ |
		CLOUDABI_RIGHT_FD_SEEK |
		CLOUDABI_RIGHT_FD_TELL |
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_STAT_FGET |
		CLOUDABI_RIGHT_FILE_STAT_GET |
		CLOUDABI_RIGHT_PROC_EXEC);

	add_fd(get_initrdfs()->get_root_fd(),
		// base rights
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_READDIR |
		CLOUDABI_RIGHT_FILE_STAT_FGET |
		CLOUDABI_RIGHT_FILE_STAT_GET,
		// inherited rights
		CLOUDABI_RIGHT_FD_READ |
		CLOUDABI_RIGHT_FD_SEEK |
		CLOUDABI_RIGHT_FD_TELL |
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_READDIR |
		CLOUDABI_RIGHT_FILE_STAT_FGET |
		CLOUDABI_RIGHT_FILE_STAT_GET |
		CLOUDABI_RIGHT_PROC_EXEC);

	auto ifstore = make_shared<ifstoresock>("ifstoresock");
	add_fd(ifstore, -1, -1);

	add_fd(get_terminal_store()->get_root_fd(),
		// base rights
		CLOUDABI_RIGHT_FILE_OPEN |
		CLOUDABI_RIGHT_FILE_READDIR |
		CLOUDABI_RIGHT_FILE_STAT_FGET |
		CLOUDABI_RIGHT_FILE_STAT_GET,
		// inherited rights
		CLOUDABI_RIGHT_FD_READ |
		CLOUDABI_RIGHT_FD_SEEK |
		CLOUDABI_RIGHT_FD_WRITE
	);
}

cloudabi_fd_t process_fd::add_fd(shared_ptr<fd_t> fd, cloudabi_rights_t rights_base, cloudabi_rights_t rights_inheriting) {
	cloudabi_fd_t fdnum;
	bool found = false;
	// TODO: this doesn't scale, make this search nonlinear
	for(fdnum = 0; fdnum < fd_capacity; ++fdnum) {
		if(fds[fdnum] == nullptr) {
			found = true;
			break;
		}
	}

	if(!found) {
		// no place for fd's, grow the fd storage
		// (note, fd_capacity is 0 for the first call of this method)
		auto old_capacity = fd_capacity;
		fd_mapping_t **old_fds = fds;

		fd_capacity += 100;

		Blk fds_blk = allocate(fd_capacity * sizeof(fd_mapping_t*));
		fds = reinterpret_cast<fd_mapping_t**>(fds_blk.ptr);

		memcpy(fds, old_fds, old_capacity * sizeof(fd_mapping_t*));
		memset(fds + old_capacity, 0, (fd_capacity - old_capacity) * sizeof(fd_mapping_t*));
		fdnum = old_capacity;

		if(old_fds != nullptr) {
			deallocate({old_fds, old_capacity * sizeof(fd_mapping_t*)});
		}
	}

	fd_mapping_t *mapping = allocate<fd_mapping_t>();
	assert(fd);
	mapping->fd = fd;
	mapping->rights_base = rights_base;
	mapping->rights_inheriting = rights_inheriting;

	fds[fdnum] = mapping;
	return fdnum;
}

cloudabi_errno_t process_fd::get_fd(fd_mapping_t **r_mapping, cloudabi_fd_t num, cloudabi_rights_t has_rights) {
	*r_mapping = nullptr;
	if(num >= fd_capacity) {
		return EBADF;
	}
	fd_mapping_t *mapping = fds[num];
	if(mapping == nullptr || !mapping->fd) {
		return EBADF;
	}
	if((mapping->rights_base & has_rights) != has_rights) {
		//get_vga_stream() << "get_fd: fd " << num << " has insufficient rights 0x" << hex << has_rights << dec << "\n";
		return ENOTCAPABLE;
	}
	*r_mapping = mapping;
	return 0;
}

cloudabi_errno_t process_fd::close_fd(cloudabi_fd_t num) {
	fd_mapping_t *mapping;
	auto res = get_fd(&mapping, num, 0);
	if(res == 0) {
		mapping->fd.reset();
		deallocate(fds[num]);
		fds[num] = nullptr;
	}
	return res;
}

cloudabi_errno_t process_fd::replace_fd(cloudabi_fd_t num, shared_ptr<fd_t> fd, cloudabi_rights_t rights_base, cloudabi_rights_t rights_inheriting) {
	fd_mapping_t *mapping;
	auto res = get_fd(&mapping, num, 0);
	if(res == 0) {
		mapping->fd = fd;
		mapping->rights_base = rights_base;
		mapping->rights_inheriting = rights_inheriting;
	}
	return res;
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

uint32_t *process_fd::ensure_get_page_table(int i) {
	if(i >= 0x300) {
		kernel_panic("process_fd::ensure_page_table() cannot answer for kernel pages");
	}
	if(page_directory[i] & 0x1 /* present */) {
		return page_tables[i];
	}

	// allocate page table
	Blk table_alloc = allocate_aligned(PAGE_SIZE, PAGE_SIZE);
	if(table_alloc.ptr == nullptr) {
		kernel_panic("Failed to allocate page table");
	}

	memset(table_alloc.ptr, 0, table_alloc.size);
	auto address = get_map_virtual()->to_physical_address(table_alloc.ptr);
	assert((reinterpret_cast<uint32_t>(address) & 0xfff) == 0);

	page_directory[i] = reinterpret_cast<uint64_t>(address) | 0x07;
	page_tables[i] = reinterpret_cast<uint32_t*>(table_alloc.ptr);
	return page_tables[i];
}

void process_fd::install_page_directory() {
	/* some sanity checks to warn early if the page directory looks incorrect */
	assert(get_map_virtual()->to_physical_address(this, reinterpret_cast<void*>(0xc00b8000)) == reinterpret_cast<void*>(0xb8000));
	assert(get_map_virtual()->to_physical_address(this, reinterpret_cast<void*>(0xc01031c6)) == reinterpret_cast<void*>(0x1031c6));

#ifndef TESTING_ENABLED
	auto page_phys_address = get_map_virtual()->to_physical_address(&page_directory[0]);
	assert((reinterpret_cast<uint32_t>(page_phys_address) & 0xfff) == 0);

	// Set the paging directory in cr3
	asm volatile("mov %0, %%cr3" : : "a"(reinterpret_cast<uint32_t>(page_phys_address)) : "memory");
#endif
}

cloudabi_errno_t process_fd::add_mem_mapping(mem_mapping_t *mapping, bool overwrite)
{
	void *begin = mapping->virtual_address;
	void *end = reinterpret_cast<char*>(begin) + mapping->number_of_pages * PAGE_SIZE;
	assert(mapping->number_of_pages > 0);

	if(overwrite) {
		// ensure the space is free
		mem_unmap(mapping->virtual_address, mapping->number_of_pages);
	}

	iterate(mappings, [&](mem_mapping_list *item) {
		void *i_begin = item->data->virtual_address;
		void *i_end = reinterpret_cast<char*>(i_begin) + item->data->number_of_pages * PAGE_SIZE;

		bool covers = end > i_begin && begin < i_end;
		assert(!(covers && overwrite)); // this should be prevented by mem_unmap
		if(covers) {
			get_vga_stream() << "Trying to create a " << mapping->number_of_pages << "-page mapping at address " << mapping->virtual_address << "\n";
			get_vga_stream() << "Found a " << item->data->number_of_pages << "-page mapping at address " << item->data->virtual_address << "\n";
			kernel_panic("add_mem_mapping(mapping, false) called for a mapping that overlaps with an existing one");
		}
	});

	mem_mapping_list *entry = allocate<mem_mapping_list>(mapping);
	append(&mappings, entry);
	return 0;

	// the page tables already contain all zeroes for this mapping. when we page
	// fault for the first time, or ensure_backed() is called on the mapping,
	// we will allocate physical pages and alter the page table.
}

/** Calls the Functor for every memory mapping in the list that falls within the given range. */
template <typename Functor>
static void iterate_mappings(mem_mapping_list **mappings, void *begin_addr, size_t num_pages, Functor f) {
	auto const PAGE_SIZE = process_fd::PAGE_SIZE;

	auto begin = reinterpret_cast<size_t>(begin_addr);
	auto end = begin + num_pages * PAGE_SIZE;
	assert(begin < end);

	mem_mapping_list *prev = nullptr;
	for(mem_mapping_list *item = *mappings; item != nullptr; item = item->next) {
		auto i_begin = reinterpret_cast<size_t>(item->data->virtual_address);
		auto i_end = i_begin + item->data->number_of_pages * PAGE_SIZE;
		assert(i_begin < i_end);

		if(end <= i_begin) {
			// this mapping is completely after the search range
			// TODO: return here, if mapping becomes sorted
		} else if(begin >= i_end) {
			// this mapping is completely before the search range
		} else if(begin <= i_begin && end >= i_end) {
			// this mapping is fully within the search range
			f(item);
		} else if(begin <= i_begin) {
			// the beginning of this mapping falls within the search range
			assert(end > i_begin);
			assert(((end - i_begin) % PAGE_SIZE) == 0);
			size_t unmap_pages = (end - i_begin) / PAGE_SIZE;

			mem_mapping_t *new_mapping = item->data->split_at(unmap_pages, false);
			mem_mapping_list *entry = allocate<mem_mapping_list>(new_mapping);
			assert(new_mapping->number_of_pages > 0);
			assert(new_mapping->virtual_address == reinterpret_cast<void*>(end));

			entry->next = item->next;
			item->next = entry;
			f(item);
			// TODO: return here, if mapping becomes sorted
		} else if(end >= i_end) {
			// the end of this mapping falls within the search range
			assert(begin > i_begin);
			assert(((begin - i_begin) % PAGE_SIZE) == 0);
			size_t pages_left = (begin - i_begin) / PAGE_SIZE;
			mem_mapping_t *new_mapping = item->data->split_at(pages_left, true);
			mem_mapping_list *entry = allocate<mem_mapping_list>(new_mapping);
			assert(new_mapping->number_of_pages > 0);
			assert(item->data->virtual_address == begin_addr);

			if(prev == nullptr) {
				*mappings = entry;
			} else {
				assert(prev->next == item);
				prev->next = entry;
			}
			entry->next = item;
			f(item);
		} else {
			// the search range is throughout the middle of this mapping
			// split it twice, such that this item is exactly the search range
			assert(((begin - i_begin) % PAGE_SIZE) == 0);
			size_t pages_left = (begin - i_begin) / PAGE_SIZE;

			assert(((end - begin) % PAGE_SIZE) == 0);
			size_t pages_middle = (end - begin) / PAGE_SIZE;

			mem_mapping_t *mapping_left = item->data->split_at(pages_left, true);
			mem_mapping_list *entry_left = allocate<mem_mapping_list>(mapping_left);
			assert(mapping_left->number_of_pages > 0);
			assert(item->data->virtual_address == begin_addr);

			mem_mapping_t *mapping_right = item->data->split_at(pages_middle, false);
			mem_mapping_list *entry_right = allocate<mem_mapping_list>(mapping_right);
			assert(mapping_right->number_of_pages > 0);
			assert(mapping_right->virtual_address == reinterpret_cast<void*>(end));

			if(prev == nullptr) {
				*mappings = entry_left;
			} else {
				assert(prev->next == item);
				prev->next = entry_left;
			}
			entry_left->next = item;
			entry_right->next = item->next;
			item->next = entry_right;
			f(item);
		}

		prev = item;
	}
}

void process_fd::mem_unmap(void *begin_addr, size_t num_pages)
{
	auto begin = reinterpret_cast<size_t>(begin_addr);
	auto end = begin + num_pages * PAGE_SIZE;
	assert(begin < end);

	// first, split the mappings if necessary
	iterate_mappings(&mappings, begin_addr, num_pages, [](mem_mapping_list*){/* ignore */});

	// then, unmap all if necessary
	remove_all(&mappings, [&](mem_mapping_list *item) {
		auto i_begin = reinterpret_cast<size_t>(item->data->virtual_address);
		auto i_end = i_begin + item->data->number_of_pages * PAGE_SIZE;
		assert(i_begin < i_end);

		if(end <= i_begin || begin >= i_end) {
			// this mapping fully survives
			return false;
		}
		else if(begin <= i_begin && end >= i_end) {
			// this mapping must be fully unmapped
			return true;
		}
		else {
			assert(!"Partial mappings should have been already split");
		}
	}, [&](mem_mapping_list *item) {
		item->data->unmap_completely();
		deallocate(item->data);
		deallocate(item);
	});
}

void process_fd::mem_protect(void *addr, size_t num_pages, cloudabi_mprot_t prot)
{
	iterate_mappings(&mappings, addr, num_pages, [prot](mem_mapping_list *item) {
		item->data->set_protection(prot);
	});
}

bool process_fd::handle_pagefault(void *addr, bool for_writing, bool for_exec)
{
	mem_mapping_t *mapping = nullptr;
	size_t page_i = 0;
	iterate(mappings, [&](mem_mapping_list *item) {
		auto pgnum = item->data->page_num(addr);
		if(pgnum != -1) {
			assert(mapping == nullptr);
			mapping = item->data;
			page_i = pgnum;
		}
	});
	if(mapping == nullptr) {
		// not a valid address
		return false;
	}
	if(for_writing && !(mapping->protection & CLOUDABI_PROT_WRITE)) {
		// writing not allowed
		return false;
	}
	if(for_exec && !(mapping->protection & CLOUDABI_PROT_EXEC)) {
		// execution not allowed
		return false;
	}

	// if the page is present, and the action should be allowed, change
	// protection (solve CoW here)

	if(mapping->is_backed(page_i)) {
		// TODO: if the action should be allowed, solve potential CoW and
		// change protection on the page
		return false;
	} else {
		// TODO: if for_writing is false, then CoW a page filled with zeroes.
		mapping->ensure_backed(page_i);
		return true;
	}
}

void *process_fd::find_free_virtual_range(size_t num_pages)
{
	uint32_t address = 0x90000000;
	while(address + num_pages * PAGE_SIZE < _kernel_virtual_base) {
		// - find the first lowest map after address
		mem_mapping_t *lowest = nullptr;
		iterate(mappings, [&](mem_mapping_list *item) {
			uint32_t virtual_address = reinterpret_cast<uint32_t>(item->data->virtual_address);
			if(virtual_address + item->data->number_of_pages * PAGE_SIZE > address) {
				if(!lowest || lowest->virtual_address > item->data->virtual_address) {
					lowest = item->data;
				}
			}
		});

		if(lowest == nullptr) {
			// No mappings yet
			return reinterpret_cast<void*>(address);
		}

		uint32_t virtual_address = reinterpret_cast<uint32_t>(lowest->virtual_address);
		if(address + num_pages * PAGE_SIZE <= virtual_address) {
			return reinterpret_cast<void*>(address);
		}

		address = virtual_address + lowest->number_of_pages * PAGE_SIZE;
	}
	return nullptr;
}

cloudabi_errno_t process_fd::exec(shared_ptr<fd_t> fd, size_t fdslen, fd_mapping_t **new_fds, void const *argdata, size_t argdatalen) {
	// read from this fd until it gives EOF, then exec(buf, buf_size)
	// TODO: memory map instead of reading it in full
	linked_list<Blk> *elf_pieces = nullptr;
	size_t total_size = 0;
	do {
		Blk blk = allocate(4096);
		assert(blk.ptr != nullptr); // TODO: deallocate
		size_t read = fd->pread(blk.ptr, blk.size, total_size);
		if(read == 0) {
			deallocate(blk);
			break;
		}
		total_size += read;
		linked_list<Blk> *piece = allocate<linked_list<Blk>>(blk);
		append(&elf_pieces, piece);
		if(read < blk.size) {
			break;
		}
	} while(fd->error == 0);

	if(fd->error != 0) {
		// TODO: deallocate
		return fd->error;
	}

	Blk elf_buffer_blk = allocate(total_size);
	uint8_t *elf_buffer = reinterpret_cast<uint8_t*>(elf_buffer_blk.ptr);
	if(elf_buffer == nullptr) {
		// TODO: deallocate
		return ENOMEM;
	}
	while(elf_pieces) {
		auto *item = elf_pieces;
		elf_pieces = item->next;

		size_t copy = item->data.size < total_size ? item->data.size : total_size;
		memcpy(elf_buffer, item->data.ptr, copy);
		elf_buffer += copy;
		total_size -= copy;

		deallocate(item->data);
		deallocate(item);
	}
	assert(elf_pieces == nullptr);
	assert(total_size == 0);

	// a null argdata has an argdatalen of 0; however, do allocate a page in this case, just keep
	// it empty
	assert(argdata != nullptr);
	Blk argdata_alloc = allocate(argdatalen == 0 ? 1 : argdatalen);
	uint8_t *argdata_buffer = reinterpret_cast<uint8_t*>(argdata_alloc.ptr);
	memcpy(argdata_buffer, argdata, argdatalen);

	char old_name[sizeof(name)];
	strncpy(old_name, name, sizeof(name));
	uint32_t *old_page_directory = page_directory;
	uint32_t **old_page_tables = page_tables;
	mem_mapping_list *old_mappings = mappings;

	strncpy(name, "exec<-", sizeof(name));
	strncat(name, fd->name, sizeof(name) - strlen(name) - 1);

	Blk page_directory_alloc = allocate_aligned(PAGE_SIZE, PAGE_SIZE);
	if(page_directory_alloc.ptr == nullptr) {
		kernel_panic("Failed to allocate process paging directory");
	}
	page_directory = reinterpret_cast<uint32_t*>(page_directory_alloc.ptr);
	memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));

	get_map_virtual()->fill_kernel_pages(page_directory);

	Blk page_tables_alloc = allocate(0x300 * sizeof(uint32_t*));
	if(page_tables_alloc.ptr == nullptr) {
		kernel_panic("Failed to allocate page table list");
	}
	page_tables = reinterpret_cast<uint32_t**>(page_tables_alloc.ptr);
	for(size_t i = 0; i < 0x300; ++i) {
		page_tables[i] = nullptr;
	}
	mappings = nullptr;
	install_page_directory();

	uint8_t *argdata_address = reinterpret_cast<uint8_t*>(0x80100000);
	mem_mapping_t *argdata_mapping = allocate<mem_mapping_t>(this, argdata_address, len_to_pages(argdatalen == 0 ? 1 : argdatalen), nullptr, 0, CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE);
	add_mem_mapping(argdata_mapping);
	argdata_mapping->ensure_completely_backed();
	memcpy(argdata_address, argdata_buffer, argdatalen);
	deallocate(argdata_alloc);

	auto res = exec(reinterpret_cast<uint8_t*>(elf_buffer_blk.ptr), elf_buffer_blk.size, argdata_address, argdatalen);
	deallocate(elf_buffer_blk);
	if(res != 0) {
		page_directory = old_page_directory;
		page_tables = old_page_tables;
		mappings = old_mappings;
		strncpy(name, old_name, sizeof(name));
		install_page_directory();
		deallocate(page_directory_alloc);
		// TODO: deallocate all page tables themselves as well
		deallocate(page_tables_alloc);
		return res;
	}

	// Close all unused FDs
	for(cloudabi_fd_t i = 0; i < fd_capacity; ++i) {
		if(fds[i] == nullptr || !fds[i]->fd) {
			continue;
		}

		bool fd_is_used = false;
		for(cloudabi_fd_t j = 0; j < fdslen; ++j) {
			// TODO: cloudabi does not allow an fd to be mapped twice in exec()
			if(new_fds[j]->fd == fds[i]->fd) {
				fd_is_used = true;
				break;
			}
		}

		if(!fd_is_used) {
			close_fd(i);
		}
	}

	for(cloudabi_fd_t i = 0; i < fdslen; ++i) {
		fds[i] = new_fds[i];
	}
	for(cloudabi_fd_t i = fdslen; i < fd_capacity; ++i) {
		fds[i] = nullptr;
	}

	// temporarily re-install the old page directory, so we unmap from the old page directory
	auto new_page_directory = page_directory;
	auto new_page_tables = page_tables;
	page_directory = old_page_directory;
	page_tables = old_page_tables;
	iterate(old_mappings, [&](mem_mapping_list *item) {
		item->data->unmap_completely();
	});
	page_directory = new_page_directory;
	page_tables = new_page_tables;

	remove_all(&old_mappings, [&](mem_mapping_list *) {
		return true;
	}, [&](mem_mapping_list *item) {
		deallocate(item->data);
		deallocate(item);
	});

	deallocate({old_page_directory, PAGE_SIZE});
	for(size_t i = 0; i < 0x300; ++i) {
		if(old_page_tables[i] != nullptr) {
			deallocate({old_page_tables[i], PAGE_SIZE});
		}
	}
	deallocate({old_page_tables, 0x300 * sizeof(uint32_t*)});

	// now, when process is scheduled again, we will return to the entrypoint of the new binary
	return 0;
}

cloudabi_errno_t process_fd::exec(uint8_t *buffer, size_t buffer_size, uint8_t *argdata, size_t argdatalen) {
	if(buffer_size < sizeof(Elf32_Ehdr)) {
		// Binary too small
		return ENOEXEC;
	}

	Elf32_Ehdr *header = reinterpret_cast<Elf32_Ehdr*>(buffer);
	if(memcmp(header->e_ident, "\x7F" "ELF", 4) != 0) {
		// Not an ELF binary
		return ENOEXEC;
	}

	if(header->e_ident[EI_CLASS] != ELFCLASS32) {
		// Not a 32-bit ELF binary
		return ENOEXEC;
	}

	if(header->e_ident[EI_DATA] != ELFDATA2LSB) {
		// Not least-significant byte first, unsupported at the moment
		return ENOEXEC;
	}

	if(header->e_ident[EI_VERSION] != 1) {
		// Not ELF version 1
		return ENOEXEC;
	}

	if(header->e_ident[EI_OSABI] != ELFOSABI_CLOUDABI
	|| header->e_ident[EI_ABIVERSION] != 0) {
		// Not CloudABI v0
		return ENOEXEC;
	}

	if(header->e_type != ET_EXEC && header->e_type != ET_DYN) {
		// Not an executable or shared object file
		// (CloudABI binaries can be shipped as shared object files,
		// which are actually executables, so that the kernel knows it
		// can map them anywhere in address space for ASLR)
		return ENOEXEC;
	}

	if(header->e_machine != EM_386) {
		// TODO: when we support different machine types, check that
		// header->e_machine is supported.
		return ENOEXEC;
	}

	if(header->e_version != EV_CURRENT) {
		// Not a current version ELF
		return ENOEXEC;
	}

	// Save the phdrs
	size_t elf_phnum = header->e_phnum;
	size_t elf_ph_size = header->e_phentsize * elf_phnum;

	if(header->e_phoff >= buffer_size || (header->e_phoff + elf_ph_size) >= buffer_size) {
		// Phdrs weren't shipped in this ELF
		return ENOEXEC;
	}

	void *elf_phdr = reinterpret_cast<uint8_t*>(0x80060000);
	mem_mapping_t *phdr_mapping = allocate<mem_mapping_t>(this, elf_phdr, len_to_pages(elf_ph_size), nullptr, 0, CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE);
	add_mem_mapping(phdr_mapping);
	phdr_mapping->ensure_completely_backed();

	memcpy(elf_phdr, buffer + header->e_phoff, elf_ph_size);

	// Map the LOAD sections
	for(size_t phi = 0; phi < elf_phnum; ++phi) {
		size_t offset = header->e_phoff + phi * header->e_phentsize;
		if(offset >= buffer_size || (offset + sizeof(Elf32_Phdr)) >= buffer_size) {
			// Phdr wasn't shipped in this ELF
			return ENOEXEC;
		}

		Elf32_Phdr *phdr = reinterpret_cast<Elf32_Phdr*>(buffer + offset);

		if(phdr->p_type == PT_LOAD) {
			if(phdr->p_offset >= buffer_size || (phdr->p_offset + phdr->p_filesz) >= buffer_size) {
				// Phdr data wasn't shipped in this ELF
				return ENOEXEC;
			}
			if((phdr->p_vaddr % PAGE_SIZE) != 0) {
				// Phdr load section wasn't aligned
				return ENOEXEC;
			}

			cloudabi_mprot_t protection = 0;
			if(phdr->p_flags & 1) {
				protection |= CLOUDABI_PROT_EXEC;
			}
			if(phdr->p_flags & 2) {
				protection |= CLOUDABI_PROT_WRITE;
			}
			if(phdr->p_flags & 4) {
				protection |= CLOUDABI_PROT_READ;
			}

			uint8_t *vaddr = reinterpret_cast<uint8_t*>(phdr->p_vaddr);
			uint8_t *code_offset = buffer + phdr->p_offset;
			mem_mapping_t *t = allocate<mem_mapping_t>(this, vaddr, len_to_pages(phdr->p_memsz), nullptr, 0, protection);
			add_mem_mapping(t);
			t->ensure_completely_backed();
			memcpy(vaddr, code_offset, phdr->p_filesz);
			memset(vaddr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
		}
	}

	// initialize vdso address
	size_t vdso_size = vdso_blob_size;
	uint8_t *vdso_address = reinterpret_cast<uint8_t*>(0x80040000);
	mem_mapping_t *vdso_mapping = allocate<mem_mapping_t>(this, vdso_address, len_to_pages(vdso_size), nullptr, 0, CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE);
	add_mem_mapping(vdso_mapping);
	vdso_mapping->ensure_completely_backed();
	memcpy(vdso_address, vdso_blob, vdso_size);

	// choose a pid
	get_random()->get(reinterpret_cast<char*>(pid), sizeof(pid));
	pid[6] = (pid[6] & 0x0f) | 0x40;
	pid[8] = (pid[8] & 0x3f) | 0x80;

	// initialize auxv
	size_t auxv_entries = 9; // including CLOUDABI_AT_NULL
	size_t auxv_size = auxv_entries * sizeof(cloudabi_auxv_t);
	uint8_t *auxv_address = reinterpret_cast<uint8_t*>(0x80010000);
	uint8_t *pid_address = auxv_address + auxv_size;
	auxv_size += sizeof(pid);
	mem_mapping_t *auxv_mapping = allocate<mem_mapping_t>(this, auxv_address, len_to_pages(auxv_size), nullptr, 0, CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE);
	add_mem_mapping(auxv_mapping);
	auxv_mapping->ensure_completely_backed();
	memcpy(pid_address, pid, sizeof(pid));
	
	cloudabi_auxv_t *auxv = reinterpret_cast<cloudabi_auxv_t*>(auxv_address);
	auto *auxv_orig = auxv;
	auxv->a_type = CLOUDABI_AT_ARGDATA;
	auxv->a_ptr = argdata;
	auxv++;
	auxv->a_type = CLOUDABI_AT_ARGDATALEN;
	auxv->a_val = argdatalen;
	auxv++;
	auxv->a_type = CLOUDABI_AT_BASE;
	auxv->a_ptr = nullptr; /* because we don't do address randomization */
	auxv++;
	auxv->a_type = CLOUDABI_AT_PAGESZ;
	auxv->a_val = PAGE_SIZE;
	auxv++;
	auxv->a_type = CLOUDABI_AT_SYSINFO_EHDR;
	auxv->a_ptr = vdso_address;
	auxv++;
	auxv->a_type = CLOUDABI_AT_PHDR;
	auxv->a_ptr = elf_phdr;
	auxv++;
	auxv->a_type = CLOUDABI_AT_PHNUM;
	auxv->a_val = elf_phnum;
	auxv++;
	auxv->a_type = CLOUDABI_AT_PID;
	auxv->a_ptr = pid_address;
	auxv++;
	auxv->a_type = CLOUDABI_AT_NULL;
	auxv++;
	assert(auxv_orig + auxv_entries == auxv);

	// detach all existing threads from the process
	// (note that one of them will be currently running to do this exec(),
	// so we can't deallocate them; this will be done in the scheduler once
	// the threads are no longer scheduled)
	exit_all_threads();

	// set running state
	running = true;
	exitcode = 0;
	exitsignal = 0;

	// create the initial stack
	size_t userland_stack_size = 0x10000 /* 64 kb */;
	uint8_t *userland_stack_top = reinterpret_cast<uint8_t*>(0x80000000);
	uint8_t *userland_stack_bottom = userland_stack_top - userland_stack_size;
	mem_mapping_t *stack_mapping = allocate<mem_mapping_t>(this, userland_stack_bottom, len_to_pages(userland_stack_size), nullptr, 0, CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE);
	add_mem_mapping(stack_mapping);
	stack_mapping->ensure_backed(len_to_pages(userland_stack_size) - 1);

	// create the main thread
	add_thread(userland_stack_bottom, userland_stack_size, auxv_address, reinterpret_cast<void*>(header->e_entry));

	return 0;
}

void process_fd::fork(shared_ptr<thread> otherthread) {
	process_fd *otherprocess = otherthread->get_process();
	assert(otherprocess->running);
	assert(otherprocess->threads);
	assert(!threads);
	assert(!mappings);

	strncpy(name, otherprocess->name, sizeof(name));
	strncat(name, "->forked", sizeof(name) - strlen(name) - 1);

	// set return values for the child. The thread constructor immediately
	// copies the state to an iret frame on the initial kernel stack, so
	// this needs to be done before the thread is constructed.
	interrupt_state_t state;
	otherthread->get_return_state(&state);
	auto old_eax = state.eax;
	auto old_edx = state.edx;
	auto old_eflags = state.eflags;
	state.eax = CLOUDABI_PROCESS_CHILD;
	state.edx = MAIN_THREAD;
	state.eflags &= ~0x1; /* set carry bit */
	otherthread->set_return_state(&state);

	auto mainthread = make_shared_aligned<thread>(16, this, otherthread);

	state.eax = old_eax;
	state.edx = old_edx;
	state.eflags = old_eflags;
	otherthread->set_return_state(&state);

	running = true;

	// dup all fd's
	fd_capacity = otherprocess->fd_capacity;
	Blk fds_blk = allocate(fd_capacity * sizeof(fd_mapping_t*));
	fds = reinterpret_cast<fd_mapping_t**>(fds_blk.ptr);
	for(cloudabi_fd_t i = 0; i < fd_capacity; ++i) {
		fd_mapping_t *old_mapping = otherprocess->fds[i];
		fd_mapping_t *mapping = nullptr;

		if(old_mapping != nullptr) {
			mapping = allocate<fd_mapping_t>();
			mapping->fd = old_mapping->fd;
			mapping->rights_base = old_mapping->rights_base;
			mapping->rights_inheriting = old_mapping->rights_inheriting;
		}

		fds[i] = mapping;
	}

	iterate(otherprocess->mappings, [&](mem_mapping_list *item) {
		mem_mapping_t *mapping = allocate<mem_mapping_t>(this, item->data);
		add_mem_mapping(mapping);

		// TODO: implement copy-on-write
		mapping->copy_from(item->data);
	});

	add_thread(mainthread);
}

void process_fd::add_thread(shared_ptr<thread> thr)
{
	auto item = allocate<thread_list>(thr);
	append(&threads, item);
	get_scheduler()->thread_ready(thr);
}

shared_ptr<thread> process_fd::add_thread(void *stack_bottom, size_t stack_len, void *auxv_address, void *entrypoint)
{
	assert(running);

	auto thr = make_shared_aligned<thread>(16, this, stack_bottom, stack_len, auxv_address, entrypoint, ++last_thread);
	add_thread(thr);
	return thr;
}

void process_fd::exit(cloudabi_exitcode_t c, cloudabi_signal_t s)
{
	if(this == global_state_->init) {
		get_vga_stream() << "init exited with signal " << s << ", exit code " << c << "\n";
		kernel_panic("init exited");
	}
	running = false;
	exitsignal = s;
	if(exitsignal == 0) {
		exitcode = c;
	} else {
		exitcode = 0;
	}

	get_vga_stream() << "Process \"" << name << "\" exited with signal " << exitsignal << ", code " << exitcode << ".\n";

	termination_signaler.condition_broadcast();

	for(size_t i = 0; i < fd_capacity; ++i) {
		close_fd(i);
	}

	// deallocation will happen in the destructor

	// unschedule all threads
	exit_all_threads();
}

void process_fd::signal(cloudabi_signal_t s)
{
	switch(s) {
	case CLOUDABI_SIGABRT:
	case CLOUDABI_SIGALRM:
	case CLOUDABI_SIGBUS:
	case CLOUDABI_SIGFPE:
	case CLOUDABI_SIGHUP:
	case CLOUDABI_SIGILL:
	case CLOUDABI_SIGINT:
	case CLOUDABI_SIGKILL:
	case CLOUDABI_SIGQUIT:
	case CLOUDABI_SIGSEGV:
	case CLOUDABI_SIGSYS:
	case CLOUDABI_SIGTERM:
	case CLOUDABI_SIGTRAP:
	case CLOUDABI_SIGUSR1:
	case CLOUDABI_SIGUSR2:
	case CLOUDABI_SIGVTALRM:
	case CLOUDABI_SIGXCPU:
	case CLOUDABI_SIGXFSZ:
		exit(0, s);
		return;
	default:
		// Signals cannot be handled in CloudABI, so signals either
		// kill the process, or are ignored.
		;
	}
}

userland_lock_waiters_t *process_fd::get_userland_lock_info(_Atomic(cloudabi_lock_t) *lock)
{
	auto res = find(userland_locks, [&](userland_lock_waiters_list *item) {
		return item->data->lock == lock;
	});
	return res == nullptr ? nullptr : res->data;
}

userland_lock_waiters_t *process_fd::get_or_create_userland_lock_info(_Atomic(cloudabi_lock_t) *lock)
{
	auto res = get_userland_lock_info(lock);
	if(res != nullptr) {
		return res;
	}

	userland_lock_waiters_t *new_info = allocate<userland_lock_waiters_t>();
	new_info->lock = lock;

	userland_lock_waiters_list *new_list = allocate<userland_lock_waiters_list>(new_info);
	append(&userland_locks, new_list);
	return new_info;
}

void process_fd::forget_userland_lock_info(_Atomic(cloudabi_lock_t) *lock)
{
	remove_one(&userland_locks, [&](userland_lock_waiters_list *item) {
		return item->data->lock == lock;
	}, [&](userland_lock_waiters_list *item) {
		deallocate(item->data);
		deallocate(item);
	});
}

userland_condvar_waiters_t *process_fd::get_userland_condvar_cv(_Atomic(cloudabi_condvar_t) *condvar)
{
	auto res = find(userland_condvars, [&](userland_condvar_waiters_list *item) {
		return item->data->condvar == condvar;
	});
	return res == nullptr ? nullptr : res->data;
}

userland_condvar_waiters_t *process_fd::get_or_create_userland_condvar_cv(_Atomic(cloudabi_condvar_t) *condvar, _Atomic(cloudabi_lock_t) *lock)
{
	auto res = get_userland_condvar_cv(condvar);
	if(res != nullptr) {
		return res;
	}

	userland_condvar_waiters_t *new_info = allocate<userland_condvar_waiters_t>();
	new_info->condvar = condvar;
	new_info->lock = lock;

	userland_condvar_waiters_list *new_list = allocate<userland_condvar_waiters_list>(new_info);
	append(&userland_condvars, new_list);
	return new_info;
}

void process_fd::forget_userland_condvar_cv(_Atomic(cloudabi_condvar_t) *condvar)
{
	remove_one(&userland_condvars, [&](userland_condvar_waiters_list *item) {
		return item->data->condvar == condvar;
	}, [&](userland_condvar_waiters_list *item) {
		deallocate(item->data);
		deallocate(item);
	});
}

void process_fd::remove_thread(shared_ptr<thread> t)
{
	bool removed = remove_one(&threads, [&t](thread_list *item) {
		return item->data == t;
	});

	(void)removed;
	assert(removed);
}

void process_fd::exit_all_threads() {
	auto thr_list = threads;
	while(thr_list) {
		auto thread_it = thr_list->data;
		thr_list = thr_list->next;
		assert(!thread_it->is_exited());
		// this call will destruct the thr_list this thread_it
		// came from, which is why we already take the ->next a bit
		// earlier
		thread_it->thread_exit();
	}
	assert(threads == nullptr);
	last_thread = MAIN_THREAD - 1;
}
