#include "global.hpp"
#include "memory/page_allocator.hpp"
#include "fd/process_fd.hpp"

extern uint32_t _kernel_virtual_base;

using namespace cloudos;

page_allocator::page_allocator(void *h, memory_map_entry *mmap, size_t mmap_size)
{
	uint64_t physical_handout = reinterpret_cast<uint64_t>(h) - _kernel_virtual_base;

	// Convert memory map into list of aligned pages + their physical address
	iterate_through_mem_map(mmap, mmap_size, [&](memory_map_entry *entry) {
		// only add pages for available memory
		if(entry->mem_type != 1) {
			return;
		}

		uint64_t begin_addr = entry->mem_base;
		uint64_t end_addr = begin_addr + entry->mem_length;

		if(begin_addr < physical_handout + sizeof(page_list)) {
			// only generate pages from the physical_handout after this page_list, or it will be unallocatable anyway
			begin_addr = physical_handout + sizeof(page_list);
		}

		begin_addr = align_up(begin_addr, PAGE_SIZE);

		while(begin_addr + PAGE_SIZE <= end_addr) {
			// if the address cannot be represented as a pointer, nevermind
			if(begin_addr >= (uint64_t(1) << 32)) {
				break;
			}

			// another page fits in this memory block, so allocate an entry
			// TODO: this assumes the first memory block in mmap is large enough to hold the page list;
			// we should probably at least check for this, and ideally, use an initial allocator for
			// allocating data structures necessary for running a true page allocator.
			page_list *page_entry = reinterpret_cast<page_list*>(physical_handout + _kernel_virtual_base);
			physical_handout += sizeof(page_list);

			new (page_entry) page_list(begin_addr);

			begin_addr += PAGE_SIZE;

			if(free_pages == nullptr) {
				free_pages = free_pages_tail = page_entry;
			} else {
				assert(free_pages_tail->next == nullptr);
				free_pages_tail->next = page_entry;
				free_pages_tail = page_entry;
			}
		}
	});

	// Now, remove any pages that we may have allocated that are already before the physical_handout
	remove_all(&free_pages, [&](page_list *entry){
		return entry->data < physical_handout;
	}, [&](page_list *) {});
}

Blk page_allocator::allocate_phys() {
	if(empty(free_pages)) {
		get_vga_stream() << __PRETTY_FUNCTION__ << " - there are no pages left\n";
		return {};
	}

	page_list *page = free_pages;
	if(free_pages == free_pages_tail) {
		free_pages_tail = page->next;
	}
	free_pages = page->next;

	page->next = used_pages;
	used_pages = page;

	return {reinterpret_cast<void*>(page->data), PAGE_SIZE};
}

Blk page_allocator::allocate_contiguous_phys(size_t num) {
	assert(num > 0);
	// TODO: sort free_pages by ptr until we have a large enough range

	page_list **head_ptr = &free_pages;
	page_list *head = free_pages, *last = free_pages;
	size_t num_found = 1;

	while(num_found < num) {
		if(last->next == nullptr) {
			get_vga_stream() << __PRETTY_FUNCTION__ << " - there are no pages left\n";
			return {};
		}

		uint64_t last_ptr = last->data;
		uint64_t next_ptr = last->next->data;

		if(last_ptr + PAGE_SIZE == next_ptr) {
			last = last->next;
			num_found++;
		} else {
			// not contiguous, start over
			head_ptr = &(last->next);
			head = last = last->next;
			num_found = 1;
		}
	}

	*head_ptr = last->next;
	last->next = used_pages;
	used_pages = head;
	return {reinterpret_cast<void*>(head->data), num * PAGE_SIZE};
}

void page_allocator::deallocate_phys(Blk b) {
	assert(!empty(used_pages));
	assert((reinterpret_cast<uintptr_t>(b.ptr) & 0xfff) == 0);
	assert((b.size % PAGE_SIZE) == 0);

	int num = b.size / PAGE_SIZE;
	for(int i = 0; i < num; ++i) {
		page_list *item = used_pages;
		used_pages = item->next;

		item->data = reinterpret_cast<uintptr_t>(b.ptr) + i * PAGE_SIZE;
		item->next = nullptr;
		if(free_pages == nullptr) {
			free_pages = free_pages_tail = item;
		} else {
			free_pages_tail->next = item;
			free_pages_tail = item;
		}
	}
}
