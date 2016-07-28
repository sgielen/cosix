#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

enum class vga_color {
	BLACK = 0,
	BLUE = 1,
	GREEN = 2,
	CYAN = 3,
	RED = 4,
	MAGENTA = 5,
	BROWN = 6,
	LIGHT_GREY = 7,
	DARK_GREY = 8,
	LIGHT_BLUE = 9,
	LIGHT_GREEN = 10,
	LIGHT_CYAN = 11,
	LIGHT_RED = 12,
	LIGHT_MAGENTA = 13,
	LIGHT_BROWN = 14,
	WHITE = 15,
};

struct vga_buffer {
	vga_buffer(vga_color fg = vga_color::LIGHT_GREY, vga_color bg = vga_color::BLACK,
		uint16_t *ptr = reinterpret_cast<uint16_t*>(0xC00B8000 /* VGA pointer in upper virtual memory, mapped to 0xb8000 in physical memory */),
		size_t width = 80, size_t height = 25);

	void set_color(vga_color fg, vga_color bg);
	void get_color(vga_color *fg, vga_color *bg);
	void set_cursor(size_t row, size_t column);
	void get_cursor(size_t *row, size_t *column);

	void putc_at(char c, vga_color fg, vga_color bg, size_t x, size_t y);
	void putc_at(char c, uint8_t color, size_t x, size_t y);
	void getc_at(char *c, uint8_t *color, size_t x, size_t y);
	void getc_at(char *c, vga_color *fg, vga_color *bg, size_t x, size_t y);
	void scroll();
	void clear_screen();
	void putc(char c);
	void write(const char *str);

private:
	size_t cursor_row;
	size_t cursor_column;
	uint8_t color;
	uint16_t *buffer;

	size_t width;
	size_t height;
};

}

