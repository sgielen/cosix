#include "x86_serial.hpp"
#include <oslibc/assert.hpp>
#include <oslibc/string.h>
#include <hw/cpu_io.hpp>
#include <global.hpp>

using namespace cloudos;

uint16_t device_to_port(uint8_t p) {
	switch(p) {
	case 1: return 0x3f8;
	case 2: return 0x2f8;
	case 3: return 0x3e8;
	case 4: return 0x2e8;
	default: kernel_panic("No such serial port");
	}
}

x86_serial::x86_serial(device *parent) : device(parent), irq_handler() {
}

x86_serial::~x86_serial() {
	get_vga_stream().set_serial(nullptr);
}

const char *x86_serial::description() {
	return "x86 serial controller";
}

cloudabi_errno_t x86_serial::init() {
	register_irq(3);
	register_irq(4);
	init_serial(1);
	init_serial(2);
	init_serial(3);
	init_serial(4);

	// send 'hello world' over COM1
	transmit_string(1, "Serial driver started.\n");
	get_vga_stream().set_serial(this);
	return 0;
}

void x86_serial::transmit(uint8_t device, const char *str, size_t len)
{
	for(size_t i = 0; i < len; ++i) {
		uint16_t base = device_to_port(device);
		while((inb(base + 5) & 0x20) == 0) {
			/* wait for buffer space */
		}
		outb(base, str[i]);
	}
}

void x86_serial::transmit_string(uint8_t device, const char *str)
{
	return transmit(device, str, strlen(str));
}

void x86_serial::init_serial(uint8_t device) {
	uint16_t base = device_to_port(device);
	outb(base + 1, 0x00); // disable interrupts
	outb(base + 3, 0x80); // set DLAB
	outb(base    , 0x0c); // low byte for divisor
	outb(base + 1, 0x00); // high byte for divisor (12 means rate 9600)
	outb(base + 3, 0x03); // 8N1
	outb(base + 2, 0xc7); //
	outb(base + 1, 0x0b); // enable IRQ for data/transmitter/status change
}

void x86_serial::handle_irq(uint8_t irq) {
	assert(irq == 3 || irq == 4);
	(void)irq;
}
