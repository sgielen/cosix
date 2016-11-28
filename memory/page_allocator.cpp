#include "global.hpp"
#include "memory/page_allocator.hpp"
#include "fd/process_fd.hpp"

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

page_allocator::page_allocator(void *h, memory_map_entry *mmap, size_t mmap_size)
: used_pages(0)
, free_pages(0)
{
	uint64_t physical_handout = reinterpret_cast<uint64_t>(h) - _kernel_virtual_base;

	// Convert memory map into list of aligned pages + their physical address
	page_list *free_pages_tail = 0;
	iterate_through_mem_map(mmap, mmap_size, [&](memory_map_entry *entry) {
		// only add pages for available memory
		if(entry->mem_type != 1) {
			return;
		}

		uint64_t begin_addr = reinterpret_cast<uint64_t>(entry->mem_base.addr);
		uint64_t end_addr = begin_addr + entry->mem_length;

		if(begin_addr < physical_handout + sizeof(page_list)) {
			// only generate pages from the physical_handout after this page_list, or it will be unallocatable anyway
			begin_addr = physical_handout + sizeof(page_list);
		}

		begin_addr = align_up(begin_addr, PAGE_SIZE);

		while(begin_addr + PAGE_SIZE <= end_addr) {
			// another page fits in this memory block, so allocate an entry
			// TODO: this assumes the first memory block in mmap is large enough to hold the page list;
			// we should probably at least check for this, and ideally, use an initial allocator for
			// allocating data structures necessary for running a true page allocator.
			page_list *page_entry = reinterpret_cast<page_list*>(physical_handout + _kernel_virtual_base);
			physical_handout += sizeof(page_list);

			page_entry->data = begin_addr;
			page_entry->next = nullptr;
			begin_addr += PAGE_SIZE;

			if(free_pages == 0) {
				free_pages = free_pages_tail = page_entry;
			} else {
				append(&free_pages_tail, page_entry);
				free_pages_tail = page_entry;
			}
		}
	});

	// Now, remove any pages that we may have allocated that are already before the physical_handout
	remove_all(&free_pages, [&](page_list *entry){
		return entry->data < physical_handout;
	}, [&](page_list*) { /* cannot deallocate */ });

	// Then, allocate memory for 1 GB of kernel pages
	for(size_t i = 0; i < NUM_KERNEL_PAGES; ++i) {
		page_allocation p;
		// cannot use allocate() yet, as it needs the kernel page tables we are currently allocating
		// but in the next loop, we will make sure to map all pages used for kernel page tables to the
		// physical memory, so we can safely use allocate_phys here
		auto res = allocate_phys(&p);
		if(res != 0) {
			kernel_panic("Failed to allocate kernel paging table");
		}
		if((reinterpret_cast<uint32_t>(p.address) & 0xfff) != 0) {
			kernel_panic("physically allocated memory is not page-aligned");
		}

		kernel_page_tables[i] = reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(p.address) + _kernel_virtual_base);
	}

	// Lastly, ensure all physical memory for page allocations up till now is mapped onto _kernel_virtual_base
	for(size_t i = 0; i < NUM_KERNEL_PAGES; ++i) {
		uint32_t *page_table = kernel_page_tables[i];

		for(size_t entry = 0; entry < PAGING_TABLE_SIZE; ++entry) {
			uint32_t address = i * PAGING_TABLE_SIZE * PAGE_SIZE + entry * PAGE_SIZE;
			if(address < free_pages->data) {
				page_table[entry] = address | 0x03; // read-write kernel-only present entry
			} else {
				page_table[entry] = 0; // read-only kernel-only non-present entry
			}
		}
	}

	// This leaves kernel_page_tables as a list of page tables, where the
	// first entries ensure that the necessary page tables are always
	// mapped in a predictable spot. Newly allocated physical memory can be
	// mapped at contiguous locations in kernel virtual memory as well.

	// Two sanity checks:
	if(to_physical_address(reinterpret_cast<void*>(0xc00b8000)) != reinterpret_cast<void*>(0xb8000)) {
		kernel_panic("Failed to map VGA page, VGA stream will fail later");
	}
	if(to_physical_address(reinterpret_cast<void*>(0xc01031c6)) != reinterpret_cast<void*>(0x1031c6)) {
		kernel_panic("Kernel will fail to execute");
	}
}

void *page_allocator::to_physical_address(const void *logical) {
	return to_physical_address(0, logical);
}

void *page_allocator::to_physical_address(process_fd *fd, const void *logical) {
	if(logical == 0) {
		return 0;
	}

	uint16_t page_table_num = reinterpret_cast<uint64_t>(logical) >> 22;
	uint32_t *page_table = 0;
	if(page_table_num >= 0x300) {
		page_table = kernel_page_tables[page_table_num - 0x300];
	} else if(fd) {
		page_table = fd->get_page_table(page_table_num);
	} else {
		kernel_panic("to_physical_address for userspace page, but no process fd given");
	}

	if(page_table == 0) {
		// not mapped
		return 0;
	}

	uint16_t page_entry_num = reinterpret_cast<uint64_t>(logical) >> 12 & 0x03ff;
	uint32_t page_entry = page_table[page_entry_num];
	if(!(page_entry & 0x1)) {
		// not mapped
		return 0;
	}

	uint32_t page_address = page_entry & 0xfffff000;
	page_address += reinterpret_cast<uint64_t>(logical) & 0xfff;
	return reinterpret_cast<void*>(page_address);
}

cloudabi_errno_t page_allocator::allocate_phys(page_allocation *a) {
	if(empty(free_pages)) {
		a->address = 0;
		a->page_ptr = 0;
		a->capacity = 0;
		get_vga_stream() << "allocate() called, but there are no pages left\n";
		return ENOMEM;
	}

	page_list *page = free_pages;
	free_pages = page->next;

	page->next = used_pages;
	used_pages = page;

	a->address = reinterpret_cast<void*>(page->data);
	a->page_ptr = page;
	a->capacity = PAGE_SIZE;

	return 0;
}

cloudabi_errno_t page_allocator::allocate(page_allocation *a) {
	auto res = allocate_phys(a);
	if(res != 0) {
		return res;
	}

	static int last_table_checked = 0;

	// TODO: find a better way of finding the next virtual address entry
	for(; last_table_checked < NUM_KERNEL_PAGES; ++last_table_checked) {
		uint32_t *page_table = kernel_page_tables[last_table_checked];
		static int last_entry_checked = 0;
		for(; last_entry_checked < PAGING_TABLE_SIZE; ++last_entry_checked) {
			if(page_table[last_entry_checked] == 0) {
				page_table[last_entry_checked] = reinterpret_cast<uint64_t>(a->address) | 0x03;
				a->address = reinterpret_cast<void*>((0x300 + last_table_checked) * PAGE_SIZE * PAGING_TABLE_SIZE + last_entry_checked * PAGE_SIZE);
				return 0;
			}
		}
		last_entry_checked = 0;
	}

	get_vga_stream() << "allocate() called, but there is no virtual address space left\n";
	return ENOMEM;
}

void page_allocator::fill_kernel_pages(uint32_t *page_directory) {
	// page_directory is the page directory of some process
	// we will fill it with the addresses of our kernel page tables, so that every
	// process always sees the same kernel page tables
	// TODO: we could mark them global, since they never change
	// TODO: we assume physaddr of these tables is virtaddr - _virtual_kernel_base

	for(size_t i = 0; i < NUM_KERNEL_PAGES; ++i) {
		uint32_t address = reinterpret_cast<uint64_t>(kernel_page_tables[i]) - _kernel_virtual_base;
		page_directory[0x300 + i] = address | 0x03 /* read-write kernel-only present table */;
	}
}
