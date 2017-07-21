#include <fd/fd.hpp>
#include <global.hpp>
#include <hw/vga.hpp>
#include <hw/vga_stream.hpp>
#include <oslibc/numeric.h>
#include <term/console_terminal.hpp>
#include <term/escape_codes.hpp>

using namespace cloudos;

console_terminal::console_terminal()
: terminal_impl("console")
{
}

cloudabi_errno_t console_terminal::write_output_token(const char *token, size_t tokensz)
{
	auto &vga = get_vga_stream().get_vga_buffer();

	// prepare a scancode to send
	char scancode[32];
	memset(scancode, 0, strlen(scancode));
	scancode[0] = 0x1b;
	scancode[1] = '[';
	size_t scancode_len = 2;

	if(!is_escape_sequence(token, tokensz)) {
		// control characters are handled by the vga_buffer itself
		for(size_t i = 0; i < tokensz; ++i) {
			vga.putc(token[i]);
		}
	} else if(is_escape_code_get_terminal_size(token, tokensz)) {
		size_t w, h;
		vga.get_size(&w, &h);

		char digits[32];

		scancode[scancode_len++] = 8;
		scancode[scancode_len++] = ';';
		scancode_len = strlcat(scancode, uitoa_s(h, digits, sizeof(digits), 10), sizeof(scancode));
		scancode[scancode_len++] = ';';
		scancode_len = strlcat(scancode, uitoa_s(w, digits, sizeof(digits), 10), sizeof(scancode));
		scancode[scancode_len++] = 't';
		queue_keystrokes_during_write(scancode, scancode_len);
	// TODO: coloring scan codes
	// TODO: cursor moving scan codes
	// TODO: scrolling / clearing screen
	} else {
		// ignore other scancodes
	}
	return 0;
}
