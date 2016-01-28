#include <hw/vga.hpp>
#include <hw/vga_stream.hpp>
#include <stdlib.h>
#include <catch.hpp>

#include <iostream>

TEST_CASE( "hw/vga_stream" ) {
	size_t const VGA_WIDTH = 80;
	size_t const VGA_HEIGHT = 25;

	size_t const VGA_BLOCK_SIZE = sizeof(uint16_t);

	/* allocate a VGA buffer with LINE_MARGIN lines above and below,
	 * initialize with some arbitrary byte */
	uint16_t vga_buf[VGA_WIDTH * VGA_HEIGHT];

	using cloudos::vga_buffer;
	using cloudos::vga_color;
	using cloudos::vga_stream;

	vga_buffer buf(vga_color::LIGHT_GREY, vga_color::BLACK, vga_buf, VGA_WIDTH, VGA_HEIGHT);
	vga_stream s(buf);

	uint16_t first_line[VGA_WIDTH];
	uint16_t inside_line[VGA_WIDTH];
	for(size_t x = 0; x < VGA_WIDTH; ++x) {
		// (LIGHT_GREY | BLACK << 4) << 8 | ' '
		first_line[x] = inside_line[x] = 0x0720;
	}

	SECTION( "streaming const char* into vga_buffer" ) {
		s << "Hello World";
		memcpy(&first_line[0], "\x48\x07\x65\x07\x6c\x07\x6c\x07\x6F\x07\x20\x07\x57\x07\x6f\x07\x72\x07\x6c\x07\x64\x07", 22);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 11);
	};

	SECTION( "streaming multiple const char* into vga_buffer" ) {
		s << "Hello " << "World";
		memcpy(&first_line[0], "\x48\x07\x65\x07\x6c\x07\x6c\x07\x6F\x07\x20\x07\x57\x07\x6f\x07\x72\x07\x6c\x07\x64\x07", 22);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 11);
	}

	SECTION( "streaming NULL const char* into vga_buffer" ) {
		const char *str = NULL;
		s << str;
		memcpy(&first_line[0], "\x28\x07\x6e\x07\x75\x07\x6c\x07\x6c\x07\x29\x07", 12);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 6);
	};

	SECTION( "streaming integers into vga_buffer" ) {
		s << -502;
		memcpy(&first_line[0], "\x2d\x07\x35\x07\x30\x07\x32\x07", 8);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 4);
	};

	SECTION( "streaming hexadecimal integers into vga_buffer" ) {
		s << cloudos::hex << -502;
		memcpy(&first_line[0], "\x2d\x07\x31\x07\x66\x07\x36\x07", 8);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 4);
	}

	SECTION( "streaming a boolean into vga_buffer" ) {
		s << true;
		memcpy(&first_line[0], "\x74\x07\x72\x07\x75\x07\x65\x07", 8);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 4);
	}

	SECTION( "streaming a hex number and a boolean into vga_buffer" ) {
		s << cloudos::hex << 0xff << true;
		memcpy(&first_line[0], "\x66\x07\x66\x07\x74\x07\x72\x07\x75\x07\x65\x07", 12);

		for(size_t y = 0; y < VGA_HEIGHT; ++y) {
			if(y == 0) {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, first_line, sizeof(first_line)) == 0);
			} else {
				REQUIRE(memcmp(vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 6);
	}
}
