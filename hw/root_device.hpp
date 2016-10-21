#pragma once

#include "hw/device.hpp"

namespace cloudos {

struct root_device : public device {
	root_device();

	const char *description() override;

	cloudabi_errno_t init() override;
};

}
