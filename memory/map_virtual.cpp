#include "global.hpp"
#include "memory/map_virtual.hpp"
#include "fd/process_fd.hpp"

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

#ifndef NDEBUG
// In debug mode, fill all allocated pages with 0xda, a marker for use
// of uninitialized memory.
// TODO: I should test regularly with various values for this member,
// since different values may lead to various kinds of bugs when memory
// is not initialized correctly.
//static const char debug_page_filler = 0xad;
static const char debug_page_filler = 0xda;
#endif

map_virtual::map_virtual(page_allocator *p)
: pa(p)
{
	// Allocate pages for the vmem bitmap
	size_t vmem_buf_size = NUM_KERNEL_PAGES / 8;
	size_t num_vmem_buf_pages = vmem_buf_size / PAGE_SIZE;
	if(vmem_buf_size % PAGE_SIZE) {
		++num_vmem_buf_pages;
	}

	/* NOTE: these are physical memory Blks */
	Blk vmem_bitmap_allocs = pa->allocate_contiguous_phys(num_vmem_buf_pages);
	assert(vmem_bitmap_allocs.size == vmem_buf_size);

	uint8_t *bitmap_buffer = reinterpret_cast<uint8_t*>(vmem_bitmap_allocs.ptr) + _kernel_virtual_base;
	memset(bitmap_buffer, 0, vmem_bitmap_allocs.size);
	vmem_bitmap.reset(NUM_KERNEL_PAGES, bitmap_buffer);

	// Allocate memory for 1 GB of kernel pages
	for(size_t i = 0; i < NUM_KERNEL_PAGE_TABLES; ++i) {
		Blk b = pa->allocate_phys();
		if(b.ptr == 0) {
			kernel_panic("Failed to allocate kernel paging table");
		}
		assert((reinterpret_cast<uint32_t>(b.ptr) & 0xfff) == 0);
		kernel_page_tables[i] = reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(b.ptr) + _kernel_virtual_base);
	}

	// Allocate memory for the stage2 page directory
	paging_directory_stage2 = pa->allocate_phys();
	if(paging_directory_stage2.ptr == 0) {
		kernel_panic("Failed to allocate page directory for stage2 paging");
	}

	Blk first_free_page = pa->allocate_phys();

	// Lastly, ensure all physical memory for page allocations up till now is mapped onto _kernel_virtual_base
	for(size_t i = 0; i < NUM_KERNEL_PAGE_TABLES; ++i) {
		uint32_t *page_table = kernel_page_tables[i];

		for(size_t entry = 0; entry < PAGING_TABLE_SIZE; ++entry) {
			uint32_t address = i * PAGING_TABLE_SIZE * PAGE_SIZE + entry * PAGE_SIZE;
			if(address < reinterpret_cast<uint32_t>(first_free_page.ptr)) {
				vmem_bitmap.set(i * PAGING_TABLE_SIZE + entry);
				page_table[entry] = address | 0x03; // read-write kernel-only present entry
			} else {
				page_table[entry] = 0; // read-only kernel-only non-present entry
			}
		}
	}

	pa->deallocate_phys(first_free_page);

	// This leaves kernel_page_tables as a list of page tables, where the
	// first entries ensure that the necessary page tables are always
	// mapped in a predictable spot. Newly allocated physical memory can be
	// mapped at contiguous locations in kernel virtual memory as well.

	// Two sanity checks:
	assert(to_physical_address(reinterpret_cast<void*>(0xc00b8000)) == reinterpret_cast<void*>(0xb8000));
	assert(to_physical_address(reinterpret_cast<void*>(0xc01031c6)) == reinterpret_cast<void*>(0x1031c6));
}

void *map_virtual::to_physical_address(const void *logical) {
	return to_physical_address(0, logical);
}

void *map_virtual::to_physical_address(process_fd *fd, const void *logical) {
	if(logical == 0) {
		return 0;
	}

	uint16_t page_table_num = reinterpret_cast<uint64_t>(logical) >> 22;
	uint32_t *page_table = 0;
	if(page_table_num >= KERNEL_PAGE_OFFSET) {
		page_table = kernel_page_tables[page_table_num - KERNEL_PAGE_OFFSET];
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

static int num_pages_for_size(size_t size) {
	size_t n = size / map_virtual::PAGE_SIZE;
	return size % map_virtual::PAGE_SIZE ? n + 1 : n;
}

Blk map_virtual::allocate_contiguous_phys(size_t size) {
	size_t num_pages = num_pages_for_size(size);
	size_t bit;
	if(!vmem_bitmap.get_contiguous_free(num_pages, bit)) {
		get_vga_stream() << "allocate_contiguous_phys() called, but there is no virtual address space left\n";
		return {};
	}

	Blk phys_alloc = pa->allocate_contiguous_phys(num_pages);
	if(phys_alloc.ptr == 0) {
		get_vga_stream() << "allocate_contiguous_phys() called, but no physical contiguous block could be found\n";
		return {};
	}

	void *first_ptr = nullptr;
	for(size_t i = 0; i < num_pages; ++i) {
		size_t page = bit + i;
		size_t table = page / PAGING_TABLE_SIZE;
		size_t entrynum = page % PAGING_TABLE_SIZE;

		assert(table < NUM_KERNEL_PAGE_TABLES);

		uint32_t &entry = kernel_page_tables[table][entrynum];
		assert(entry == 0);
		uint64_t ptr = reinterpret_cast<uint64_t>(phys_alloc.ptr) + i * PAGE_SIZE;
		entry = ptr | 0x03;
		if(i == 0) {
			table += KERNEL_PAGE_OFFSET;
			first_ptr = reinterpret_cast<void*>(table * PAGING_TABLE_SIZE * PAGE_SIZE + entrynum * PAGE_SIZE);
		}
	}

#ifndef NDEBUG
	memset(first_ptr, debug_page_filler, size);
#endif

	return {first_ptr, size};
}

Blk map_virtual::allocate(size_t size) {
	size_t num_pages = num_pages_for_size(size);
	size_t bit;
	if(!vmem_bitmap.get_contiguous_free(num_pages, bit)) {
		get_vga_stream() << "allocate() called, but there is no virtual address space left\n";
		return {};
	}

	void *first_ptr = nullptr;
	for(size_t i = 0; i < num_pages; ++i) {
		size_t page = bit + i;
		size_t table = page / PAGING_TABLE_SIZE;
		size_t entrynum = page % PAGING_TABLE_SIZE;

		assert(table < NUM_KERNEL_PAGE_TABLES);

		uint32_t &entry = kernel_page_tables[table][entrynum];
		assert(entry == 0);

		Blk b = pa->allocate_phys();
		if(b.ptr == 0) {
			get_vga_stream() << "allocate() called, but there are no pages left\n";
			// TODO: free earlier acquired pages
			return {};
		}

		entry = reinterpret_cast<uint64_t>(b.ptr) | 0x03;
		if(i == 0) {
			table += KERNEL_PAGE_OFFSET;
			first_ptr = reinterpret_cast<void*>(table * PAGING_TABLE_SIZE * PAGE_SIZE + entrynum * PAGE_SIZE);
		}
	}

#ifndef NDEBUG
	memset(first_ptr, debug_page_filler, size);
#endif

	return {first_ptr, size};
}

void map_virtual::deallocate(Blk b) {
	size_t num_pages = num_pages_for_size(b.size);

	for(size_t page = 0; page < num_pages; ++page) {
		void *ptr = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(b.ptr) + PAGE_SIZE * page);
		auto *phys_addr = to_physical_address(ptr);
		assert(phys_addr != 0);

		// Unmap virtual page
		unmap_page_only(ptr);

		// Allow handing out the physical page again
		pa->deallocate_phys({reinterpret_cast<void*>(phys_addr), PAGE_SIZE});
	}
}

void map_virtual::unmap_page_only(void *virtual_address) {
	uint32_t addr = reinterpret_cast<uint32_t>(virtual_address);
	uint16_t page_table_num = (addr >> 22) - KERNEL_PAGE_OFFSET;
	uint16_t page_entry_num = addr >> 12 & 0x03ff;

	// Mark the virtual page as unused, also flush TLB cache
	kernel_page_tables[page_table_num][page_entry_num] = 0;
	asm volatile ( "invlpg (%0)" : : "b"(addr) : "memory");

	// Allow handing out the virtual page again
	vmem_bitmap.unset(page_table_num * PAGING_TABLE_SIZE + page_entry_num);
}

void map_virtual::fill_kernel_pages(uint32_t *page_directory) {
	// page_directory is the page directory of some process
	// we will fill it with the addresses of our kernel page tables, so that every
	// process always sees the same kernel page tables
	// TODO: we could mark them global, since they never change
	// TODO: we assume physaddr of these tables is virtaddr - _virtual_kernel_base

	for(size_t i = 0; i < NUM_KERNEL_PAGE_TABLES; ++i) {
		uint32_t address = reinterpret_cast<uint64_t>(kernel_page_tables[i]) - _kernel_virtual_base;
		page_directory[KERNEL_PAGE_OFFSET + i] = address | 0x03 /* read-write kernel-only present table */;
	}
}

void map_virtual::load_paging_stage2() {
	auto *page_directory = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(paging_directory_stage2.ptr) + _kernel_virtual_base);
	memset(page_directory, 0, PAGE_DIRECTORY_SIZE * sizeof(uint32_t));
	fill_kernel_pages(page_directory);

	assert(to_physical_address(reinterpret_cast<void*>(0xc00b8000)) == reinterpret_cast<void*>(0xb8000));
	assert(to_physical_address(reinterpret_cast<void*>(0xc01031c6)) == reinterpret_cast<void*>(0x1031c6));

	asm volatile("mov %0, %%cr3" : : "a"(reinterpret_cast<uint32_t>(paging_directory_stage2.ptr)) : "memory");
}

void map_virtual::free_paging_stage2() {
	assert(paging_directory_stage2.ptr != 0);
	pa->deallocate_phys(paging_directory_stage2);
	paging_directory_stage2.ptr = 0;
}
