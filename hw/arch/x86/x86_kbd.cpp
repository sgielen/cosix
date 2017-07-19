#include "x86_kbd.hpp"
#include <global.hpp>
#include <hw/cpu_io.hpp>
#include <oslibc/assert.hpp>
#include <oslibc/ctype.h>
#include <oslibc/string.h>
#include <term/terminal.hpp>
#include <term/terminal_store.hpp>

using namespace cloudos;

const uint8_t CTRL_MODIFIER = 0x01;
const uint8_t SHIFT_MODIFIER = 0x02;
const uint8_t ALT_MODIFIER = 0x04;
const uint8_t META_MODIFIER = 0x08;

const uint8_t LED_SCROLL = 1;
const uint8_t LED_NUM = 2;
const uint8_t LED_CAPS = 4;

static const char scancode_to_character[] = {
	0   , '\e', '1' , '2' , '3' , '4' , '5' , '6' , // 00-07
	'7' , '8' , '9' , '0' , '-' , '=' , '\b', '\t', // 08-0f
	'q' , 'w' , 'e' , 'r' , 't' , 'y' , 'u' , 'i' , // 10-17
	'o' , 'p' , '[' , ']' , '\n', 0   , 'a' , 's' , // 18-1f
	'd' , 'f' , 'g' , 'h' , 'j' , 'k' , 'l' , ';' , // 20-27
	'\'', '`' , 0   , '\\', 'z' , 'x' , 'c' , 'v' , // 28-2f
	'b' , 'n' , 'm' , ',' , '.' , '/' , 0   , 0   , // 30-37
	0   , ' ' , 0   , 0   , 0   , 0   , 0   , 0   , // 38-3f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 40-47
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 48-4f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 50-57
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 58-5f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 60-67
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 68-6f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 70-77
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 78-7f
};

static const char scancode_to_character_shift[] = {
	0   , '\e', '!' , '@' , '#' , '$' , '%' , '^' , // 00-07
	'&' , '*' , '(' , ')' , '_' , '+' , '\b', '\t', // 08-0f
	'Q' , 'W' , 'E' , 'R' , 'T' , 'Y' , 'U' , 'I' , // 10-17
	'O' , 'P' , '{' , '}' , '\n', 0   , 'A' , 'S' , // 18-1f
	'D' , 'F' , 'G' , 'H' , 'J' , 'K' , 'L' , ':' , // 20-27
	'"' , '~' , 0   , '|' , 'Z' , 'X' , 'C' , 'V' , // 28-2f
	'B' , 'N' , 'M' , '<' , '>' , '?' , 0   , 0   , // 30-37
	0   , ' ' , 0   , 0   , 0   , 0   , 0   , 0   , // 38-3f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 40-47
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 48-4f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 50-57
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 58-5f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 60-67
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 68-6f
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 70-77
	0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , // 78-7f
};

static void convert_scancode_to_sequence(uint8_t scancode, uint8_t *modifiers, uint8_t *ledstate, char *sequence, size_t *size) {
	assert(*size >= 32);

	bool isbreak = scancode & 0x80; // if true, key is unpressed
	scancode = scancode & 0x7f;

	// Update modifiers
	uint8_t modifier = 0;
	switch(scancode) {
	case 0x1d: modifier = CTRL_MODIFIER; break;
	case 0x2a /* left shift */ : modifier = SHIFT_MODIFIER; break;
	case 0x36 /* right shift */: modifier = SHIFT_MODIFIER; break;
	case 0x38 /* left alt */   : modifier = ALT_MODIFIER; break;
	case 0x5b /* left meta */  : modifier = META_MODIFIER; break;
	case 0x5c /* right meta */ : modifier = META_MODIFIER; break;
	case 0x60 /* right alt */  : modifier = ALT_MODIFIER; break;
	}

	if(modifier != 0) {
		if(isbreak) {
			*modifiers &= ~modifier;
		} else {
			*modifiers |= modifier;
		}
		*size = 0;
		return;
	}

	if(isbreak) {
		// not interested in unpressed keys
		*size = 0;
		return;
	}

	// is it a regular character?
	assert(scancode < sizeof(scancode_to_character_shift));
	assert(scancode < sizeof(scancode_to_character));
	char lowercase = scancode_to_character[scancode];
	char uppercase = scancode_to_character_shift[scancode];
	char character = ((*modifiers & SHIFT_MODIFIER) || ((*ledstate & LED_CAPS) && isalpha(lowercase))) ? uppercase : lowercase;
	if(character != 0) {
		sequence[0] = character;
		*size = 1;
		return;
	}

	// does it have a special ^[Ox / ^[[x escape sequence?
	if(*modifiers == 0) {
		char type;
		char special;
		switch(scancode) {
		case 0x48 /* up */:    type = '['; special = 'A'; break;
		case 0x50 /* down */:  type = '['; special = 'B'; break;
		case 0x4d /* right */: type = '['; special = 'C'; break;
		case 0x4b /* left */:  type = '['; special = 'D'; break;

		case 0x47 /* home */:  type = '['; special = 'H'; break;
		case 0x4f /* end */:   type = '['; special = 'F'; break;

		case 0x3b /* f1 */:    type = 'O'; special = 'P'; break;
		case 0x3c /* f2 */:    type = 'O'; special = 'Q'; break;
		case 0x3d /* f3 */:    type = 'O'; special = 'R'; break;
		case 0x3e /* f4 */:    type = 'O'; special = 'S'; break;

		default:               type = 0; special = 0;
		}

		if(special) {
			sequence[0] = 0x1b;
			sequence[1] = type;
			sequence[2] = special;
			*size = 3;
			return;
		}
	}

	// does it have a special ^[[XY / ^[[X;MY escape sequence?
	uint8_t m = 0;
	switch(*modifiers & (CTRL_MODIFIER | SHIFT_MODIFIER | ALT_MODIFIER)) {
	case 0: m = 1; break;
	case SHIFT_MODIFIER:   m = 2; break;
	case ALT_MODIFIER:     m = 3; break;
	case SHIFT_MODIFIER | ALT_MODIFIER:  m = 4; break;
	case CTRL_MODIFIER:    m = 5; break;
	case SHIFT_MODIFIER | CTRL_MODIFIER: m = 6; break;
	case ALT_MODIFIER | CTRL_MODIFIER:   m = 7; break;
	case SHIFT_MODIFIER | ALT_MODIFIER | CTRL_MODIFIER: m = 8; break;
	}
	if(*modifiers & META_MODIFIER) {
		m += 8;
	}
	assert(m != 0);

	uint8_t x = 0;
	uint8_t y = 0;
	switch(scancode) {
	case 0x48 /* up */:     x = 1; y = 'A'; break;
	case 0x50 /* down */:   x = 1; y = 'B'; break;
	case 0x4d /* right */:  x = 1; y = 'C'; break;
	case 0x4b /* left */:   x = 1; y = 'D'; break;

	case 0x47 /* home */:   x = 1; y = 'H'; break;
	case 0x4f /* end */:    x = 1; y = 'F'; break;

	case 0x49 /* page up */:x = 5; y = '~'; break;
	case 0x51 /* page dn */:x = 6; y = '~'; break;
	case 0x52 /* insert */: x = 2; y = '~'; break;
	case 0x53 /* delete */: x = 3; y = '~'; break;

	case 0x3b /* f1 */:     x = 11; y = '~'; break;
	case 0x3c /* f2 */:     x = 12; y = '~'; break;
	case 0x3d /* f3 */:     x = 13; y = '~'; break;
	case 0x3e /* f4 */:     x = 14; y = '~'; break;
	case 0x3f /* f5 */:     x = 15; y = '~'; break;
	case 0x40 /* f6 */:     x = 17; y = '~'; break;
	case 0x41 /* f7 */:     x = 18; y = '~'; break;
	case 0x42 /* f8 */:     x = 19; y = '~'; break;
	case 0x43 /* f9 */:     x = 20; y = '~'; break;
	case 0x44 /* f10 */:    x = 21; y = '~'; break;
	case 0x57 /* f11 */:    x = 23; y = '~'; break;
	case 0x58 /* f12 */:    x = 24; y = '~'; break;

	case 0x37 /* prtscr */: x = 25; y = '~'; break;

	case 0x4a /* numpad - */:
		sequence[0] = '-';
		*size = 1;
		return;

	case 0x4e /* numpad + */:
		sequence[0] = '+';
		*size = 1;
		return;

	case 0x4c /* numpad 5 */:
		sequence[0] = '5';
		*size = 1;
		return;
	}

	if(x != 0 && y != 0) {
		*size = 0;
		sequence[(*size)++] = 0x1b;
		sequence[(*size)++] = '[';
		if(x < 10) {
			sequence[(*size)++] = 0x30 + x;
		} else {
			assert(x < 100);
			sequence[(*size)++] = 0x30 + (x / 10);
			sequence[(*size)++] = 0x30 + (x % 10);
		}

		if(m != 1) {
			sequence[(*size)++] = ';';
			if(m < 10) {
				sequence[(*size)++] = 0x30 + m;
			} else {
				assert(m < 100);
				sequence[(*size)++] = 0x30 + (m / 10);
				sequence[(*size)++] = 0x30 + (m % 10);
			}
		}

		sequence[(*size)++] = y;
		return;
	}

	get_vga_stream() << "Unknown keypress: scancode 0x" << hex << scancode << ", modifiers 0x" << hex << *modifiers << "\n";
	*size = 0;
}

x86_kbd::x86_kbd(device *parent) : device(parent), irq_handler() {
	memset(scancode_buffer, 0, sizeof(scancode_buffer));
}

const char *x86_kbd::description() {
	return "x86 keyboard controller";
}

cloudabi_errno_t x86_kbd::init() {
	register_irq(1);

	// clear the keyboard buffer
	while(get_scancode_raw()) {}

	set_ledstate();
	return 0;
}

uint8_t x86_kbd::get_scancode_raw() {
	// wait for the ready bit to turn on
	uint32_t waits = 0;
	while((inb(0x64) & 0x1) == 0 && waits < 0xfffff) {
		// TODO: usleep for a short while
		waits++;
	}

	if((inb(0x64) & 0x1) != 0x1) {
		// Keyboard doesn't have any character. This can happen at boot, when
		// the keyboard sent an IRQ but we already read its character.
		return 0;
	}

	return inb(0x60);
}

uint8_t x86_kbd::get_scancode() {
	// do we still have scancodes in the buffer we received while waiting for
	// an ack/resend?
	if(scancode_buffer[0] == 0) {
		return get_scancode_raw();
	} else {
		uint8_t res = scancode_buffer[0];
		memmove(scancode_buffer, scancode_buffer + 1, sizeof(scancode_buffer) - 1);
		scancode_buffer[sizeof(scancode_buffer)-1] = 0;
		return res;
	}
}

void x86_kbd::delay_scancode(uint8_t scancode) {
	for(size_t i = 0; i < sizeof(scancode_buffer); ++i) {
		if(scancode_buffer[i] == 0) {
			scancode_buffer[i] = scancode;
			return;
		}
	}
}

bool x86_kbd::led_enabled(uint8_t led) {
	return ledstate & led;
}

void x86_kbd::toggle_led(uint8_t led) {
	if(led_enabled(led)) {
		disable_led(led);
	} else {
		enable_led(led);
	}
}

void x86_kbd::enable_led(uint8_t led) {
	if(!led_enabled(led)) {
		ledstate |= led;
		set_ledstate();
	}
}

void x86_kbd::disable_led(uint8_t led) {
	if(led_enabled(led)) {
		ledstate &= ~led;
		set_ledstate();
	}
}

void x86_kbd::send_cmdbyte(uint8_t cmd) {
	uint32_t waits = 0;
	while(waits++ < 0xff) {
		outb(0x60, cmd);
		while(waits++ < 0xff) {
			uint8_t sc = get_scancode_raw();
			if(sc == 0xfa) {
				return;
			} else if(sc == 0xfe) {
				break;
			} else if(sc != 0) {
				delay_scancode(sc);
			}
		}
	}
	kernel_panic("x86_kbd: failed to get response to cmdbyte");
}

void x86_kbd::set_ledstate() {
	ignore_irq = true;
	send_cmdbyte(0xed);
	send_cmdbyte(ledstate);
	ignore_irq = false;

	// pump any scancodes that may have come through while irqs were disabled
	while(scancode_buffer[0] != 0) {
		pump_character();
	}
}

void x86_kbd::handle_irq(uint8_t irq) {
	assert(irq == 1);
	(void)irq;

	if(ignore_irq) {
		// don't get_scancode(); any scancodes we might miss will be
		// saved into the scancode_buffer and will be returned later
		return;
	}

	pump_character();
}

void x86_kbd::pump_character() {
	uint8_t scancode = get_scancode();
	switch(scancode) {
	case 0x00: /* error from the keyboard, or no key to read */ return;
	case 0x3a: toggle_led(LED_CAPS); return;
	case 0x45: toggle_led(LED_NUM); return;
	case 0x46: toggle_led(LED_SCROLL); return;
	case 0xfa: /* ACK, ignore */ return;
	case 0xfe: /* repeat cmd, already done in send_cmdbyte, ignore */ return;
	default: /* handle below */ ;
	}

	// Convert scancode into escape sequences
	char sequence[32];
	size_t seqsize = sizeof(sequence);
	convert_scancode_to_sequence(scancode, &modifiers, &ledstate, &sequence[0], &seqsize);

	// Send to console terminal
	if(seqsize > 0) {
		get_terminal_store()->get_terminal("console")->write_keystrokes(sequence, seqsize);
	}
}
