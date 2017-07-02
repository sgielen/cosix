#include <memory/allocation.hpp>
#include <memory/allocator.hpp>
#include <global.hpp>

using namespace cloudos;

Blk cloudos::allocate(size_t n) {
	return get_allocator()->allocate(n);
}

Blk cloudos::allocate_aligned(size_t n, size_t alignment) {
	return get_allocator()->allocate_aligned(n, alignment);
}

void cloudos::deallocate(Blk b) {
	return get_allocator()->deallocate(b);
}

