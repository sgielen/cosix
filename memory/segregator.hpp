#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/allocation.hpp>
#include <oslibc/assert.hpp>

namespace cloudos {

template <size_t threshold, typename SmallAllocator, typename LargeAllocator>
struct Segregator {
	Segregator(SmallAllocator *s, LargeAllocator *l)
	: small(s), large(l)
	{}

	Blk allocate_aligned(size_t s, size_t alignment) {
		Blk res;
		if(s < threshold) {
			res = small->allocate_aligned(s, alignment);
		} else {
			res = large->allocate_aligned(s, alignment);
		}
		assert(res.ptr == 0 || res.size == s);
		assert(res.ptr == 0 || reinterpret_cast<uintptr_t>(res.ptr) % alignment == 0);
		return res;
	}

	Blk allocate(size_t s) {
		Blk res;
		if(s < threshold) {
			res = small->allocate(s);
		} else {
			res = large->allocate(s);
		}
		assert(res.ptr == 0 || res.size == s);
		return res;
	}

	void deallocate(Blk &s) {
		if(s.size < threshold) {
			return small->deallocate(s);
		} else {
			return large->deallocate(s);
		}
	}

private:
	SmallAllocator *small;
	LargeAllocator *large;
};

}
