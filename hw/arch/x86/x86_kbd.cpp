#include "x86_kbd.hpp"
#include <oslibc/assert.hpp>
#include <global.hpp>
#include <hw/cpu_io.hpp>

using namespace cloudos;

static const char scancode_to_key[] = {
	0   , 0   , '1' , '2' , '3' , '4' , '5' , '6' , // 00-07
	'7' , '8' , '9' , '0' , '-' , '=' , '\b', '\t', // 08-0f
	'q' , 'w' , 'e' , 'r' , 't' , 'y' , 'u' , 'i' , // 10-17
	'o' , 'p' , '[' , ']' , '\n' , 0  , 'a' , 's' , // 18-1f
	'd' , 'f' , 'g' , 'h' , 'j' , 'k' , 'l' , ';' , // 20-27
	'\'', '`' , 0   , '\\', 'z' , 'x' , 'c' , 'v' , // 28-2f
	'b' , 'n' , 'm' , ',' , '.' , '/' , 0   , '*' , // 30-37
	0   , ' ' , 0   , 0   , 0   , 0   , 0   , 0   , // 38-3f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , '7' , // 40-47
	'8' , '9' , '-' , '4' , '5' , '6' , '+' , '1' , // 48-4f
	'2' , '3' , '0' , '.' // 50-53
};

x86_kbd::x86_kbd(device *parent) : device(parent), irq_handler() {
}

const char *x86_kbd::description() {
	return "x86 keyboard controller";
}

cloudabi_errno_t x86_kbd::init() {
	register_irq(1);
	return 0;
}

void x86_kbd::handle_irq(uint8_t irq) {
	assert(irq == 1);
	UNUSED(irq);

	// keyboard input!
	// wait for the ready bit to turn on
	uint32_t waits = 0;
	while((inb(0x64) & 0x1) == 0 && waits < 0xfffff) {
		waits++;
	}

	if((inb(0x64) & 0x1) == 0x1) {
		uint16_t scancode = inb(0x60);
		char buf[2];
		buf[0] = scancode_to_key[scancode];
		buf[1] = 0;
		if(scancode <= 0x53) {
			get_vga_stream() << buf;
		}
	} else {
		get_vga_stream() << "Waited for scancode for too long\n";
	}
}
