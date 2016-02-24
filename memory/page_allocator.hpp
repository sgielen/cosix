#pragma once

#include <stddef.h>
#include <stdint.h>
#include "oslibc/list.hpp"
#include "oslibc/error.h"
#include "hw/multiboot.hpp"

namespace cloudos {

typedef linked_list<uint64_t> page_list;

template <typename Functor>
void iterate_through_mem_map(memory_map_entry *m, size_t mmap_size, Functor f) {
	uint8_t *mmap = reinterpret_cast<uint8_t*>(m);
	while(mmap_size > 0) {
		memory_map_entry &entry = *reinterpret_cast<memory_map_entry*>(mmap);
		if(entry.entry_size == 0) {
			return;
		}
		f(&entry);
		mmap_size -= entry.entry_size + 4;
		mmap += entry.entry_size + 4;
	}
}

inline uint64_t align_up(uint64_t value, uint64_t alignment) {
	auto misalignment = value % alignment;
	if(misalignment != 0) {
		value += alignment - misalignment;
	}
	return value;
}

struct page_allocation {
	void *address;
	page_list *page_ptr;
	size_t capacity;
};

/**
 * This struct is responsible for allocating pages in physical memory and
 * mapping them in virtual memory. Setting up paging and enabling memory
 * allocation is one of the first tasks of the kernel as it boots. This code
 * assumes that the kernel is loaded in the first pages of physical memory,
 * identity mapped onto the first pages of virtual memory as well as in upper
 * memory, and running from upper memory (EIP-wise and stack-wise).
 */
struct page_allocator {
	page_allocator(void *handout_start, memory_map_entry *mmap, size_t memory_map_bytes);

	void install();

	void *to_physical_address(void*);

	error_t allocate(page_allocation*);

	// TODO: produce some statistics
	// TODO: allow deallocating a page, which will take the entry pointer
	// from used_pages, unmap it from the virtual address space and put it
	// back in free_pages

private:
	error_t allocate_phys(page_allocation*);

	page_list *used_pages;
	page_list *free_pages;

	static const int PAGE_SIZE = 4096 /* bytes */;
	static const int PAGING_DIRECTORY_SIZE = 1024 /* entries */;
	static const int PAGING_TABLE_SIZE = 1024 /* entries */;
	static const int PAGING_ALIGNMENT = 4096 /* bytes for entry alignment */;
	static const uint32_t kernel_address_offset = 0xc0000000 /* 3 GB */;

	inline uint32_t *get_page_table(int i) {
		if(directory != nullptr && directory[i] & 0x1 /* present */) {
			return reinterpret_cast<uint32_t*>(directory[i] & 0xfffff000 + kernel_address_offset);
		} else {
			return nullptr;
		}
	}

	uint32_t *directory;
};

}
