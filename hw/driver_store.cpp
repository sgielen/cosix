#include "hw/driver_store.hpp"

using namespace cloudos;

driver_store::driver_store()
: drivers_(nullptr)
{}

void driver_store::register_driver(driver *d) {
#ifndef NDEBUG
	if(contains_object(drivers_, d)) {
		kernel_panic("driver registering to driver store is already registered");
	}
#endif

	driver_list *new_entry = get_allocator()->allocate<driver_list>();
	new_entry->data = d;
	new_entry->next = nullptr;

	append(&drivers_, new_entry);
}
