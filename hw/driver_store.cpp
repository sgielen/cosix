#include <hw/driver_store.hpp>
#include <memory/allocation.hpp>

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

	driver_list *new_entry = allocate<driver_list>(d);
	append(&drivers_, new_entry);
}
