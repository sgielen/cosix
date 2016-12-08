#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

struct Blk {
	Blk() : ptr(nullptr), size(0), requested_size(0) {}
	Blk(void *p, size_t s, size_t r) : ptr(p), size(s), requested_size(r) {}
	void *ptr;
	size_t size;
	size_t requested_size;
};

Blk allocate(size_t n);
void deallocate(Blk b);

}
