#pragma once

#include "oslibc/error.h"

namespace cloudos {

struct device;
struct vga_stream;

struct device_list {
	device *device;
	device_list *next;
};

void dump_device_descriptions(vga_stream &stream, device *device);

/**
 * A device represents any component inside or connected to the system that is
 * considered part of the system. For example, a hard drive, a PCI bus, or a
 * network card, but not anything connected to the network card by ethernet.
 */
struct device {
	device(device *parent);
	virtual ~device();

	/**
	 * This method returns a single line of text that describes this
	 * device. This value must be either static memory, or allocated and
	 * owned by the device, and must never be freed by the caller. The
	 * value may become invalid or dangling after subsequent calls to
	 * methods of this device.
	 */
	virtual const char *description() = 0;

	/**
	 * This method initializes the device and prepares it for further
	 * communication. It will always be called before any of the other
	 * methods. It is legal for this function to change the return value of
	 * description() or to free the memory description() returned earlier.
	 */
	virtual error_t init() = 0;

	/**
	 * This method returns the parent of this device. This returns a null
	 * pointer for the root device.
	 */
	device *parent();

	/**
	 * This method returns the head of a linked-list of children of this
	 * device, or NULL if this device has no children. This value, or any
	 * value it points towards, may become invalid or dangling after
	 * subsequent calls to methods of this device or to one of its
	 * children.
	 */
	device_list *children();

private:
	void add_child(device *child);
	void remove_child(device *child);

	device *parent_;
	device_list *children_;
};

};
