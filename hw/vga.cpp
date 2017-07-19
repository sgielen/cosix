#include <oslibc/string.h>
#include <hw/vga.hpp>

using cloudos::vga_color;
using cloudos::vga_buffer;

static uint8_t make_color(vga_color fg, vga_color bg) {
	return uint8_t(fg) | uint8_t(bg) << 4;
}

vga_buffer::vga_buffer(vga_color fg, vga_color bg, uint16_t *p, size_t w, size_t h)
: cursor_row(0)
, cursor_column(0)
, color(make_color(fg, bg))
, buffer(p)
, width(w)
, height(h)
{
	clear_screen();
}

void vga_buffer::clear_screen()
{
	for (size_t y = 0; y < height; ++y) {
		for (size_t x = 0; x < width; ++x) {
			putc_at(' ', color, x, y);
		}
	}
}

void vga_buffer::set_color(vga_color fg, vga_color bg) {
	color = make_color(fg, bg);
}

void vga_buffer::get_color(vga_color *fg, vga_color *bg) {
	if(fg) *fg = vga_color(color & 0x0f);
	if(bg) *bg = vga_color((color >> 4) & 0x0f);
}

void vga_buffer::set_cursor(size_t row, size_t column) {
	cursor_row = row;
	cursor_column = column;
}

void vga_buffer::get_cursor(size_t *row, size_t *column) {
	if(row) *row = cursor_row;
	if(column) *column = cursor_column;
}

void vga_buffer::get_size(size_t *w, size_t *h) {
	if(w) *w = width;
	if(h) *h = height;
}

void vga_buffer::putc_at(char c, vga_color fg, vga_color bg, size_t x, size_t y) {
	putc_at(c, make_color(fg, bg), x, y);
}

void vga_buffer::putc_at(char c, uint8_t col, size_t x, size_t y) {
	const size_t index = y * width + x;
	buffer[index] = uint16_t(c) | uint16_t(col) << 8;
}

void vga_buffer::getc_at(char *c, uint8_t *col, size_t x, size_t y) {
	const size_t index = y * width + x;
	uint16_t block = buffer[index];
	if(c) {
		*c = uint8_t(block & 0xff);
	}
	if(col) {
		*col = uint8_t((block >> 8) & 0xff);
	}
}

void vga_buffer::getc_at(char *c, vga_color *fg, vga_color *bg, size_t x, size_t y) {
	char ch;
	uint8_t col;
	getc_at(&ch, &col, x, y);
	if(c) *c = ch;
	if(fg) *fg = vga_color(col & 0x0f);
	if(bg) *bg = vga_color((col >> 4) & 0x0f);
}

void vga_buffer::putc(char c) {
	switch(c) {
	case '\n':
		if(++cursor_row == height) {
			scroll();
		}
		// TODO: \n should not fallthrough here; to move the cursor
		// back to column 0 \r\n should be written (this is done by the
		// terminal layer normally)
		[[clang::fallthrough]];
	case '\r':
		cursor_column = 0;
		break;
	case '\b':
		if(cursor_column == 0) {
			if(cursor_row != 0) {
				--cursor_row;
				cursor_column = width - 1;
			}
		} else {
			--cursor_column;
		}
		break;
	case '\t':
		while(cursor_column % 8) {
			putc(' ');
		}
		break;
	case '\x1b':
		putc('^');
		putc('[');
		break;
	default:
		putc_at(c, color, cursor_column, cursor_row);
		if (++cursor_column == width) {
			cursor_column = 0;
			if (++cursor_row == height) {
				scroll();
			}
		}
	}
}

void vga_buffer::scroll() {
	for(size_t y = 1; y < height; ++y) {
		for(size_t x = 0; x < width; ++x) {
			char prev_char;
			uint8_t prev_color;
			getc_at(&prev_char, &prev_color, x, y);
			putc_at(prev_char, prev_color, x, y-1);
		}
	}

	for(size_t x = 0; x < width; ++x) {
		putc_at(' ', color, x, height - 1);
	}

	if(cursor_row > 0) {
		--cursor_row;
	}
}

void vga_buffer::write(const char *data) {
	size_t datalen = strlen(data);
	for (size_t i = 0; i < datalen; ++i) {
		putc(data[i]);
	}
}
