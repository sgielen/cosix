#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/allocation.hpp>

namespace cloudos {

template <size_t threshold, typename SmallAllocator, typename LargeAllocator>
struct Segregator {
	Segregator(SmallAllocator *s, LargeAllocator *l)
	: small(s), large(l)
	{}

	Blk allocate(size_t s) {
		if(s < threshold) {
			return small->allocate(s);
		} else {
			return large->allocate(s);
		}
	}

	void deallocate(Blk &s) {
		if(s.requested_size < threshold) {
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
