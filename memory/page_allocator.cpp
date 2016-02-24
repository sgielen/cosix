#include "global.hpp"
#include "memory/page_allocator.hpp"

using namespace cloudos;

page_allocator::page_allocator(void *h, memory_map_entry *mmap, size_t mmap_size)
: used_pages(0)
, free_pages(0)
, directory(0)
{
	uint64_t physical_handout = reinterpret_cast<uint64_t>(h);

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
			page_list *page_entry = reinterpret_cast<page_list*>(physical_handout + kernel_address_offset);
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

	// now, remove any pages that we may have allocated that are already before the physical_handout
	remove_all(&free_pages, [&](page_list *entry){
		return entry->data < physical_handout;
	}, [&](page_list*) { /* cannot deallocate */ });

	// Allocate and assign page tables for 3-4 GB of kernel virtual memory
	// * The first virtual pages, from 3 GB until 3 GB + $physical_handout, map to the first pages of physical memory
	// * Further pages can be allocated for them as needed
	// * Page tables for 1-3 GB of userland virtual memory are allocated per process
	// * As soon as 1 GB is not enough anymore:
	//   * Switch to x86-64 (strongly preferred)
	//   * Swap out kernel memory
	//   * Allow using virtual memory below the 3 GB boundary for kernel memory?
	//     (How to synchronize this to other processes as well?)
	// TODO: we don't actually check whether our allocations in this method
	// fit in the available memory in the memory map; we only start doing that
	// as soon as whole pages are allocated...

	page_allocation p;
	auto res = allocate_phys(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate paging directory");
	}
	directory = reinterpret_cast<uint32_t*>(reinterpret_cast<uint64_t>(p.page_ptr->data) + kernel_address_offset);

	for(size_t i = 0; i < PAGING_DIRECTORY_SIZE; ++i) {
		directory[i] = 0; // read-only kernel-only non-present table
	}

	// Allocate page tables
	constexpr uint32_t kernel_address_space = UINT32_MAX - kernel_address_offset + 1;
	constexpr size_t num_kernel_pages = kernel_address_space / PAGE_SIZE;
	constexpr size_t page_tables_needed = num_kernel_pages / PAGING_TABLE_SIZE;
	constexpr size_t first_kernel_page_table = PAGING_DIRECTORY_SIZE - page_tables_needed;
	static_assert(PAGING_ALIGNMENT == sizeof(uint32_t) * PAGING_TABLE_SIZE, "Page table must be by itself paging-aligned");
	for(size_t i = first_kernel_page_table; i < PAGING_DIRECTORY_SIZE; ++i) {
		res = allocate_phys(&p);
		if(res != error_t::no_error) {
			kernel_panic("Failed to allocate kernel paging table");
		}

		directory[i] = reinterpret_cast<uint64_t>(p.page_ptr->data) | 0x03 /* read-write kernel-only present table */;
	}

	// at this point, we have <page_tables_needed> page table addresses in our paging directory, and
	// <physical_handout> points at the first page that is not yet handed out.

	for(size_t i = 0; i < page_tables_needed; ++i) {
		uint32_t *page_table = get_page_table(first_kernel_page_table + i);
		for(size_t entry = 0; entry < PAGING_TABLE_SIZE; ++entry) {
			uint32_t address = i * PAGING_TABLE_SIZE * PAGE_SIZE + entry * PAGE_SIZE;
			if(address < free_pages->data) {
				page_table[entry] = address | 0x03; // read-write kernel-only present entry
			} else {
				page_table[entry] = 0; // read-only kernel-only non-present entry
			}
		}
	}

	// Allocate a page for the first table as well, so we can identity map it for now
	res = allocate(&p);
	if(res != error_t::no_error) {
		kernel_panic("Failed to allocate identity paging table");
	}
	uint32_t *first_page_table = reinterpret_cast<uint32_t*>(p.address);
	directory[0] = p.page_ptr->data | 0x03;
	for(size_t i = 0; i < PAGING_TABLE_SIZE; ++i) {
		first_page_table[i] = (i * 0x1000) | 0x3; // read-write kernel-only present entry
	}
}

void page_allocator::install() {
#ifndef TESTING_ENABLED
	// Set the paging directory in cr3
	asm volatile("mov %0, %%cr3" : : "a"(reinterpret_cast<uint32_t>(&directory[0]) - kernel_address_offset) : "memory");

	// Turn on paging in cr0
	int cr0;
	asm volatile("mov %%cr0, %0" : "=a"(cr0));
	cr0 |= 0x80000000;
	asm volatile("mov %0, %%cr0" : : "a"(cr0) : "memory");
#endif
}

void *page_allocator::to_physical_address(void *logical) {
	if(logical == 0) {
		return 0;
	}

	uint16_t page_table_num = reinterpret_cast<uint64_t>(logical) >> 22;
	uint32_t *page_table = get_page_table(page_table_num);
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

error_t page_allocator::allocate_phys(page_allocation *a) {
	if(empty(free_pages)) {
		kernel_panic("allocate() called, but there are no pages left");
	}

	page_list *page = free_pages;
	free_pages = page->next;

	page->next = used_pages;
	used_pages = page;

	a->address = 0;
	a->page_ptr = page;
	a->capacity = PAGE_SIZE;

	return error_t::no_error;
}

error_t page_allocator::allocate(page_allocation *a) {
	auto res = allocate_phys(a);
	if(res != error_t::no_error) {
		return res;
	}

	// TODO: find a better way of finding the next virtual address entry
	for(int table = 0x300; table < PAGING_DIRECTORY_SIZE; ++table) {
		uint32_t *page_table = get_page_table(table);
		if(!page_table) {
			continue;
		}
		for(size_t entry = 0; entry < PAGING_TABLE_SIZE; ++entry) {
			if(page_table[entry] == 0) {
				page_table[entry] = a->page_ptr->data | 0x03;
				a->address = reinterpret_cast<void*>(table * PAGE_SIZE * PAGING_TABLE_SIZE + entry * PAGE_SIZE);
				return error_t::no_error;
			}
		}
	}

	kernel_panic("allocate() called, but there is no virtual address space left");
}
