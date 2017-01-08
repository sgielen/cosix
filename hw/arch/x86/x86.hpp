#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <stdint.h>

namespace cloudos {

struct x86_driver : public driver {
	const char *description() override;
	device *probe_root_device(device *root) override;
};

/**
 * This device represents a standard x86 PC. As its children, it has all the
 * devices we normally expect on such a PC, such as the timer and the keyboard
 * controller.
 */
struct x86_pc : public device {
	x86_pc(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;
};

}
