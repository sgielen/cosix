#include "allocator.hpp"
#include "global.hpp"
#include "memory/page_allocator.hpp"

using namespace cloudos;

allocator::allocator()
: handout(0)
, handout_end(0)
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
			handout_end = handout + a.capacity;
		} else if(handout_end != a.address) {
			get_vga_stream() << "Warning: Allocator got non-sequential handout, dropping already acquired memory\n";
			handout = reinterpret_cast<uint8_t*>(a.address);
			handout_end = handout + a.capacity;
			capacity = a.capacity;
		} else {
			handout_end += a.capacity;
		}
	}

	void *ptr = handout;
	handout += x;
	capacity -= x;
	return ptr;
}
