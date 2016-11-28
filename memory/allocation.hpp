#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

struct Blk {
	void *ptr;
	size_t size;
	size_t requested_size;
};

Blk allocate(size_t n);
void deallocate(Blk b);

}
