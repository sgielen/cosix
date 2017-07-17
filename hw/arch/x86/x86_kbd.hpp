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

private:
	void pump_character();
	uint8_t get_scancode_raw();
	bool ignore_irq = false;

	uint8_t scancode_buffer[8];
	uint8_t get_scancode();
	void delay_scancode(uint8_t sc);

	uint8_t ledstate = 0;

	bool led_enabled(uint8_t led);
	void toggle_led(uint8_t led);
	void enable_led(uint8_t led);
	void disable_led(uint8_t led);
	void set_ledstate();

	void send_cmdbyte(uint8_t cmd);

	uint8_t modifiers = 0;
};

}
