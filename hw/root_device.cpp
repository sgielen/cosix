#include "hw/root_device.hpp"
#include "hw/driver.hpp"
#include "hw/driver_store.hpp"

using namespace cloudos;

root_device::root_device()
: device(nullptr)
{}

const char *root_device::description() {
	return "Root device";
}

error_t root_device::init() {
	auto list = get_driver_store()->get_drivers();
	while(list) {
		auto *dev = list->driver->probe_root_device(this);
		if(dev != nullptr) {
			auto res = dev->init();
			if(res != error_t::no_error) {
				return res;
			}
		}
		list = list->next;
	}
	return error_t::no_error;
}
