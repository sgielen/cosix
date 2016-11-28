#include <memory/allocation.hpp>
#include <memory/allocator.hpp>
#include <global.hpp>

using namespace cloudos;

Blk cloudos::allocate(size_t n) {
	return get_allocator()->get_allocator()->allocate(n);
}

void cloudos::deallocate(Blk b) {
	return get_allocator()->get_allocator()->deallocate(b);
}

