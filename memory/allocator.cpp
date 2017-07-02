#include "allocator.hpp"
#include "global.hpp"

using namespace cloudos;

allocator::allocator()
: large_bucketizer(get_map_virtual())
, small_bucketizer(get_map_virtual())
, large_segregator(&large_bucketizer, get_map_virtual())
, small_segregator(&small_bucketizer, &large_segregator)
#ifndef NDEBUG
, allocation_tracker(&small_segregator)
#endif
{
}
