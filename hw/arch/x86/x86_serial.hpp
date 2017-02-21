#pragma once

#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <stdint.h>

namespace cloudos {

/**
 * This device represents a standard x86 serial controller. It handles IRQ 3
 * and 4.
 */
struct x86_serial : public device, public irq_handler {
	x86_serial(device *parent);
	~x86_serial();

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;

	void transmit(uint8_t device, const char *str, size_t len);
	void transmit_string(uint8_t device, const char *str);

private:
	void init_serial(uint8_t device);
};

}
