#pragma once

#include <stddef.h>
#include <stdint.h>
#include "oslibc/list.hpp"
#include "oslibc/error.h"
#include "hw/multiboot.hpp"
#include "memory/allocation.hpp"

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

	Blk allocate_contiguous_phys(size_t num);
	Blk allocate_phys();
	void deallocate_phys(Blk b);
	static const int PAGE_SIZE = 4096 /* bytes */;

private:
	page_list *used_pages = nullptr;
	page_list *free_pages = nullptr;
	page_list *free_pages_tail = nullptr;
};

}
