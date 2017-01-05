#include "allocator.hpp"
#include "global.hpp"

using namespace cloudos;

allocator::allocator()
: large_bucketizer(get_map_virtual())
, small_bucketizer(get_map_virtual())
, large_segregator(&large_bucketizer, get_map_virtual())
, small_segregator(&small_bucketizer, &large_segregator)
{
}

void *allocator::allocate(size_t n) {
	Blk allocation = small_segregator.allocate(n);
	//get_vga_stream() << "Warning: " << allocation.size << " bytes leaking because of old allocator usage\n";
	return allocation.ptr;
}

void *allocator::allocate_aligned(size_t n, size_t alignment) {
	Blk allocation = small_segregator.allocate_aligned(n, alignment);
	//get_vga_stream() << "Warning: " << allocation.size << " bytes leaking because of old allocator usage\n";
	return allocation.ptr;
}
