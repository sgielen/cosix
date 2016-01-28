#include "hw/driver_store.hpp"

using namespace cloudos;

driver_store::driver_store()
: drivers_(nullptr)
{}

void driver_store::register_driver(driver *d) {
	driver_list *new_entry = get_allocator()->allocate<driver_list>();
	new_entry->driver = d;
	new_entry->next = nullptr;

	if(drivers_ == nullptr) {
		drivers_ = new_entry;
		return;
	}

	driver_list *list = drivers_;
	while(true) {
#ifndef NDEBUG
		if(list->driver == d) {
			kernel_panic("driver registering is already registered");
		}
#endif
		if(list->next == nullptr) {
			list->next = new_entry;
			return;
		}

		list = list->next;
	}
}
