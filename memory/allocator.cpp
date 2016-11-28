#include "allocator.hpp"
#include "global.hpp"

using namespace cloudos;

allocator::allocator()
: large_bucketizer(get_page_allocator())
, multiple_page(get_page_allocator())
, small_bucketizer(get_page_allocator())
, large_segregator(&large_bucketizer, &multiple_page)
, small_segregator(&small_bucketizer, &large_segregator)
{
}

void *allocator::allocate(size_t n) {
	Blk allocation = small_segregator.allocate(n);
	//get_vga_stream() << "Warning: " << allocation.size << " bytes leaking because of old allocator usage\n";
	return allocation.ptr;
}
