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
	auto *list = get_driver_store()->get_drivers();
	error_t res = error_t::no_error;
	find(list, [this, &res](driver_list *item) {
		auto *dev = item->data->probe_root_device(this);
		if(dev != nullptr) {
			res = dev->init();
			return res != error_t::no_error;
		}
		return false;
	});
	return res;
}
