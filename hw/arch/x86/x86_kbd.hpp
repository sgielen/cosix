#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <stdint.h>

namespace cloudos {

/**
 * This device represents a standard x86 keyboard controller. It handles IRQ 1.
 */
struct x86_kbd : public device, public irq_handler {
	x86_kbd(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;
};

}
