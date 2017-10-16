#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <stdint.h>

namespace cloudos {

/**
 * This device represents the two x86 ATA controllers. It primarily serves to handle interrupts.
 */
struct x86_ata : public device, public irq_handler {
	x86_ata(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;
};

}
