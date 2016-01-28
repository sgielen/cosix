#include "hw/device.hpp"
#include "memory/allocator.hpp"
#include "global.hpp"

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
#endif

	device_list *new_entry = get_allocator()->allocate<device_list>();
	new_entry->device = child;
	new_entry->next = nullptr;

	if(children_ == nullptr) {
		children_ = new_entry;
		return;
	}

	device_list *list = children_;
	while(true) {
#ifndef NDEBUG
		if(list->device == child) {
			kernel_panic("device added as child is already a child of mine");
		}
#endif
		if(list->next == nullptr) {
			list->next = new_entry;
			return;
		} else {
			list = list->next;
		}
	}
}

void device::remove_child(device *child) {
#ifndef NDEBUG
	if(child->parent() != this) {
		kernel_panic("device to remove as child does not have me as parent");
	}
#endif

	device_list *last = nullptr;
	device_list *list = children_;
	while(list) {
		if(list->device == child) {
			if(last == nullptr) {
				children_ = list->next;
			} else {
				last->next = list->next;
			}
			return;
		}
		last = list;
		list = list->next;
	}

	kernel_panic("device to remove as child was not in my children list");
}

static void dump_device_descriptions(vga_stream &stream, device *device, int level)
{
	for(int i = 0; i < level; ++i) {
		stream << ' ';
	}
	stream << "* " << device->description() << '\n';
	for(device_list *list = device->children(); list; list = list->next) {
		dump_device_descriptions(stream, list->device, level + 1);
	}
}

void cloudos::dump_device_descriptions(vga_stream &stream, device *device)
{
	::dump_device_descriptions(stream, device, 0);
}
