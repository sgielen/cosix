#pragma once
#include <stdint.h>
#include <stddef.h>

namespace cloudos {

struct memory_map_entry;

class allocator {
public:
	allocator(void *handout_start, memory_map_entry *mmap, size_t memory_map_bytes);
	void *allocate(size_t x);

private:
	bool find_next_block();

	uint64_t handout_start;
	memory_map_entry *mmap;
	size_t memory_map_bytes;
	size_t memory_map_pos;
};

};
