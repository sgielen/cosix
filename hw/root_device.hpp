#pragma once

#include "hw/device.hpp"

namespace cloudos {

struct root_device : public device {
	root_device();

	const char *description() override;

	error_t init() override;
};

}
