#include "allocator.hpp"
#include "hw/multiboot.hpp"

using namespace cloudos;

allocator::allocator(void *h, memory_map_entry *m, size_t b)
: handout_start(reinterpret_cast<uint64_t>(h))
, mmap(m)
, memory_map_bytes(b)
, memory_map_pos(0)
{
	bool block_found = false;

	// find the block the given handout_start is in
	while(memory_map_bytes > 0) {
		memory_map_entry &entry = mmap[memory_map_pos];
		if(entry.entry_size == 0) {
			// end of memory map
			mmap = 0;
			break;
		} else if(entry.entry_size != 20) {
			// TODO: this is valid per specification, but this code isn't equipped to
			// handle it, because we take the memory_map_pos as index into a C array of
			// fixed-size structs above
			mmap = 0;
			break;
		}

		uint64_t block_start = reinterpret_cast<uint64_t>(entry.mem_base.addr);
		uint64_t block_end = block_start + entry.mem_length;

		if(block_start <= handout_start && handout_start <= block_end
		&& (entry.mem_type == 1 /* available */ || entry.mem_type == 3 /* available with ACPI */)) {
			block_found = true;
			break;
		} else {
			// not this block or it's unavailable, move to the next block
			memory_map_bytes -= entry.entry_size;
			++memory_map_pos;
			continue;
		}
	}

	if(!block_found) {
		// invalid memory given as handout start, invalidate ourselves
		// TODO: log this
		mmap = 0;
	}
}

bool allocator::find_next_block() {
	// find the next usable block after memory_map_pos
	++memory_map_pos;
	while(memory_map_bytes > 0) {
		memory_map_entry &entry = mmap[memory_map_pos];
		if(entry.entry_size == 0) {
			// end of memory map
			mmap = 0;
			return false;
		} else if(entry.entry_size != 20) {
			// TODO: this is valid per specification, but this code isn't equipped to
			// handle it, because we take the memory_map_pos as index into a C array of
			// fixed-size structs above
			mmap = 0;
			return false;
		}

		if(entry.mem_type != 1 /* available */ && entry.mem_type != 3 /* available with ACPI */) {
			// unavailable, move to the next block
			memory_map_bytes -= entry.entry_size;
			++memory_map_pos;
			continue;
		}
		return true;
	}
	return false;
}

void *allocator::allocate(size_t x) {
	if(mmap == 0) {
		return NULL;
	}

	memory_map_entry &entry = mmap[memory_map_pos];

	uint64_t block_start = reinterpret_cast<uint64_t>(entry.mem_base.addr);
	uint64_t block_end = block_start + entry.mem_length;
	// assert(entry.mem_base.addr <= handout_start <= block_end);
	size_t length_remaining = block_end - handout_start;
	if(x <= length_remaining) {
		uint64_t result = handout_start;
		handout_start += x;
		return reinterpret_cast<void*>(result);
	} else {
		while(true) {
			// find the next block that does have space
			if(!find_next_block()) {
				return NULL;
			}

			memory_map_entry &new_entry = mmap[memory_map_pos];
			if(new_entry.mem_length >= x) {
				handout_start = reinterpret_cast<uint64_t>(new_entry.mem_base.addr) + x;
				return new_entry.mem_base.addr;
			}
		}
	}
}
