#pragma once

#include <stddef.h>
#include <stdint.h>
#include "oslibc/list.hpp"
#include "oslibc/error.h"
#include "oslibc/bitmap.hpp"
#include "hw/multiboot.hpp"
#include "memory/allocation.hpp"
#include "memory/page_allocator.hpp"

namespace cloudos {

struct page_allocator;

/**
 * This struct is responsible for mapping pages in kernel virtual memory.
 */
struct map_virtual {
	map_virtual(page_allocator *allocator);

	// for kernel data
	void *to_physical_address(const void*);
	// for userland and kernel data
	void *to_physical_address(process_fd*, const void*);

	Blk allocate_contiguous_phys(size_t bytes);
	Blk allocate(size_t bytes);
	void deallocate(Blk b);

	Blk map_pages_only(void *physaddr, size_t bytes);
	void unmap_pages_only(Blk alloc);
	void unmap_page_only(void *logical_address);

	Blk allocate_aligned(size_t s, size_t alignment) {
		// Our allocations are always 4096-bytes aligned, other alignments
		// are unsupported
		if((4096 % alignment) == 0) {
			auto res = allocate(s);
			assert(res.ptr == 0 || reinterpret_cast<uintptr_t>(res.ptr) % alignment == 0);
			return res;
		} else {
			return {};
		}
	}

	void fill_kernel_pages(uint32_t *page_directory);

	void load_paging_stage2();
	void free_paging_stage2();

	static constexpr int PAGE_SIZE = page_allocator::PAGE_SIZE;

private:
	static constexpr int PAGE_DIRECTORY_SIZE = 1024 /* entries */;
	static constexpr int PAGING_TABLE_SIZE = 1024 /* entries */;
	static constexpr int PAGING_ALIGNMENT = 4096 /* bytes for entry alignment */;
	static constexpr int NUM_KERNEL_PAGE_TABLES = 0x100 /* number of page tables for the kernel */;
	static constexpr int NUM_KERNEL_PAGES = NUM_KERNEL_PAGE_TABLES * PAGING_TABLE_SIZE;
	static constexpr int KERNEL_PAGE_OFFSET = 0x300 /* number of page tables before the kernel's */;

	page_allocator *pa;
	Bitmap vmem_bitmap;

	// NOTE: phys Blk
	Blk paging_directory_stage2;

	// kernel_page_tables is filled with pointers to the page tables for
	// the kernel. These values are copied into every process page
	// directory, so that the kernel pages are mapped into every process.
	// The allocations are already page-aligned.
	uint32_t *kernel_page_tables[NUM_KERNEL_PAGE_TABLES];
};

}
