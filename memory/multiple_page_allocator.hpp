#pragma once

namespace cloudos {

template <typename PageAllocator>
struct MultiplePageAllocator
{
	MultiplePageAllocator(PageAllocator *p)
	: page_allocator(p)
	{}

	Blk allocate(size_t s) {
		size_t number_of_pages = s / PageAllocator::PAGE_SIZE;
		if(s % PageAllocator::PAGE_SIZE) {
			number_of_pages++;
		}

		// TODO: implement allocate_contiguous in the page allocator
		// For now, we allocate pages until we have a large enough contiguous
		// list
		char *address_first = nullptr;
		size_t pages_allocated = 0;
		while(pages_allocated < number_of_pages) {
			page_allocation alloc;
			auto errno = page_allocator->allocate(&alloc);
			if(errno != 0) {
				kernel_panic("Failed to allocate a page");
			}

			if(address_first == nullptr) {
				address_first = reinterpret_cast<char*>(alloc.address);
				pages_allocated = 1;
			} else if(alloc.address == address_first + pages_allocated * PageAllocator::PAGE_SIZE) {
				pages_allocated++;
			} else {
				// TODO: ensure pages allocated until now are freed after finding
				// a contiguous range
				get_vga_stream() << "Non-contiguous pages returned by page allocator, dropping.\n";
				address_first = reinterpret_cast<char*>(alloc.address);
				pages_allocated = 1;
			}
		}

		return {address_first, pages_allocated * PageAllocator::PAGE_SIZE, s};
	}

	void deallocate(Blk &) {
		// TODO: implement deallocate in the page allocator
	}

private:
	PageAllocator *page_allocator;
};

}
