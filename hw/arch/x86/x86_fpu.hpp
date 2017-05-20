#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <stdint.h>

namespace cloudos {

/**
 * This device represents an x86 FPU. It primarily serves to handle exceptions.
 */
struct x86_fpu : public device, public irq_handler {
	x86_fpu(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;
};

}
