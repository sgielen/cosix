#include "allocator.hpp"
#include "global.hpp"
#include "memory/page_allocator.hpp"

using namespace cloudos;

allocator::allocator()
: handout(0)
, capacity(0)
{
}

void *allocator::allocate(size_t x) {
	while(capacity < x) {
		page_allocation a;
		if(get_page_allocator()->allocate(&a) != error_t::no_error) {
			kernel_panic("Page allocator failed to allocate");
		}
		capacity += a.capacity;
		if(handout == 0) {
			handout = reinterpret_cast<uint8_t*>(a.address);
		}
	}

	void *ptr = handout;
	handout += x;
	capacity -= x;
	return ptr;
}
