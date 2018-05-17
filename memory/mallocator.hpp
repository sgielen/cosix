#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/allocation.hpp>
#include <oslibc/assert.hpp>

namespace cloudos {

#ifdef TESTING_ENABLED

struct Mallocator {
	Mallocator()
	{}

	Blk allocate_aligned(size_t s, size_t alignment) {
		if(alignment == 4096) {
			return {valloc(s), s};
		} else if(alignment == 1 || alignment == 2 || alignment == 4 || alignment == 8 || alignment == 16) {
			return {malloc(s), s};
		} else {
			return {};
		}
	}

	Blk allocate(size_t s) {
		return {malloc(s), s};
	}

	void deallocate(Blk &s) {
		if(s.ptr == nullptr) {
			abort();
		}
		free(s.ptr);
	}
};

#endif

}
