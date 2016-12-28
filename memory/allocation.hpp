#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

struct Blk {
	Blk() : ptr(nullptr), size(0) {}
	Blk(void *p, size_t s) : ptr(p), size(s) {}
	void *ptr;
	size_t size;
};

Blk allocate(size_t n);
Blk allocate_aligned(size_t n, size_t alignment);
void deallocate(Blk b);

}
