#pragma once

#include <memory/page_allocator.hpp>
#include <memory/allocation.hpp>
#include <global.hpp>
#include <oslibc/assert.hpp>

namespace cloudos {

template <typename PageAllocator>
struct BucketizerBin {
	void initialize(PageAllocator *p, size_t a) {
		parent = p;
		allocsize = a;
		assert(allocsize > sizeof(void*));
		tail = nullptr;
	}

	void fill() {
		page_allocation alloc;
		auto errno = parent->allocate(&alloc);
		if(errno != 0) {
			kernel_panic("Failed to allocate a page");
		}

		char *address = reinterpret_cast<char*>(alloc.address);
		size_t num_blocks = alloc.capacity / allocsize;
		/* At least two blocks, otherwise we could've just allocated
		 * a full page here
		 */
		assert(num_blocks >= 2);
		assert(alloc.capacity == PageAllocator::PAGE_SIZE);

		for(size_t i = 0; i < num_blocks; ++i) {
			void *block_address = reinterpret_cast<void*>(address + i * allocsize);
			// append this to the tail
			*reinterpret_cast<void**>(block_address) = tail;
			tail = block_address;
		}
	}

	bool empty() {
		return tail == nullptr;
	}

	Blk allocate() {
		if(empty()) {
			// no blocks available
			return {nullptr, 0, 0};
		} else {
			// replace tail with *tail, return old tail
			void *ptr = tail;
			tail = *reinterpret_cast<void**>(tail);
			return {ptr, allocsize, 0 /* filled in by Bucketizer */};
		}
	}

	void deallocate(Blk b) {
		assert(b.size == allocsize);
		*reinterpret_cast<void**>(b.ptr) = tail;
		tail = b.ptr;

/*
		// Check if we can return a page to the page allocator now
		void *page_start = b.ptr % PageAllocator::PAGE_SIZE;
		void *page_end = page_start + PageAllocator::PAGE_SIZE;
		size_t num_blocks = PageAllocator::PAGE_SIZE / allocsize;

		size_t blocks_found = 0;
		void *cur = tail;
		while(cur != nullptr && blocks_found < num_blocks) {
			if(cur >= page_start && cur < page_end) {
				blocks_found++;
			}
			cur = *reinterpret_cast<void**>(cur);
		}

		if(blocks_found == num_blocks) {
			// all blocks are currently in the freelist (or some block
			// was double-freed); returning the page
			// TODO: return the page
			// TODO: remove all slabs from this page from the freelist
		}
*/
	}

private:
	PageAllocator *parent;
	size_t allocsize;
	// pointer to a block of memory that contains a pointer
	// to the next block of memory at the start (or null if
	// this is the end of the list)
	void *tail;
};

template <typename PageAllocator, int min, int max, int step>
struct Bucketizer {
	static constexpr size_t numbins = (max - min) / step;
	typedef BucketizerBin<PageAllocator> Bin;

	Bucketizer(PageAllocator *parent)
	{
		// Ensure numbins is not rounded down
		static_assert(((max - min) % step) == 0, "max - min must be divisible by step");
		// Ensure sanity
		static_assert(max > min, "max must be greater than min");
		static_assert(step <= (max - min), "must be at least one full bin");

		for(size_t i = 0; i < numbins; ++i) {
			bins[i].initialize(parent, min + (i+1) * step);
		}
	}

	Blk allocate(size_t s) {
		auto &bin = get_bin_sized(s);
		if(bin.empty()) {
			bin.fill();
		}
		auto allocation = bin.allocate();
		allocation.requested_size = s;
		assert(allocation.size >= s);
		return allocation;
	}

	void deallocate(Blk &s) {
		assert(s.size >= sizeof(void*));
		auto &bin = get_bin_sized(s.size);
		bin.deallocate(s);
	}

private:
	inline size_t get_upper_bound(size_t s) {
		assert(s >= min);
		assert(s <= max);
		size_t offset = s - min;
		return s + ((step - offset) % step);
	}

	Bin &get_bin_sized(size_t s) {
		assert(s >= min);
		assert(s <= max);
		if(s == min) {
			return bins[0];
		} else if(((s - min) % step) == 0) {
			return bins[(s - min) / step - 1];
		} else {
			return bins[(s - min) / step];
		}
	}

	Bin bins[numbins];
};

}
