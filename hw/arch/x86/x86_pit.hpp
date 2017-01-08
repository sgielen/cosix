#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <stdint.h>

namespace cloudos {

/**
 * This device represents a standard x86 PIT. It handles IRQ 0.
 */
struct x86_pit : public device, public irq_handler {
	x86_pit(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;
};

}
