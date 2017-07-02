#include <global.hpp>
#include <hw/device.hpp>
#include <memory/allocation.hpp>

using namespace cloudos;

device::device(device *parent)
: parent_(parent)
, children_(nullptr)
{
	if(parent_) {
		parent_->add_child(this);
	}
}

device::~device() {
	if(children_ != nullptr) {
		kernel_panic("device destructed while it still has children");
	}
	if(parent_) {
		parent_->remove_child(this);
	}
}
device *device::parent() {
	return parent_;
}

device_list *device::children() {
	return children_;
}

void device::add_child(device *child) {
#ifndef NDEBUG
	if(child->parent() != this) {
		kernel_panic("device to add as child does not have me as parent");
	}

	if(contains_object(children_, child)) {
		kernel_panic("device added as child is already a child of mine");
	}
#endif

	device_list *new_entry = allocate<device_list>(child);
	append(&children_, new_entry);
}

void device::remove_child(device *child) {
#ifndef NDEBUG
	if(child->parent() != this) {
		kernel_panic("device to remove as child does not have me as parent");
	}
#endif

	if(!remove_object(&children_, child)) {
		kernel_panic("device to remove as child was not in my children list");
	}
}

void device::timer_event_recursive() {
	timer_event();
	iterate(children_, [](device_list *item) {
		item->data->timer_event_recursive();
	});
}

static void dump_device_descriptions(vga_stream &stream, device *device, int level)
{
	for(int i = 0; i < level; ++i) {
		stream << ' ';
	}
	stream << "* " << device->description() << '\n';
	iterate(device->children(), [&stream, level](device_list *item) {
		dump_device_descriptions(stream, item->data, level + 1);
	});
}

void cloudos::dump_device_descriptions(vga_stream &stream, device *device)
{
	::dump_device_descriptions(stream, device, 0);
}
