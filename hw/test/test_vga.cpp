#include <hw/vga.hpp>
#include <oslibc/string.h>
#include <catch.hpp>

#include <iostream>

TEST_CASE( "hw/vga" ) {
	size_t const VGA_WIDTH = 80;
	size_t const VGA_HEIGHT = 25;

	size_t const LINE_MARGIN = 2;
	size_t const VGA_HEIGHT_ALLOC = VGA_HEIGHT + 2 * LINE_MARGIN;
	size_t const VGA_BLOCK_SIZE = sizeof(uint16_t);

	/* allocate a VGA buffer with LINE_MARGIN lines above and below,
	 * initialize with some arbitrary byte */
	uint16_t larger_than_vga_buf[VGA_WIDTH * VGA_HEIGHT_ALLOC];
	memset(&larger_than_vga_buf[0], 0x3b, sizeof(larger_than_vga_buf));

	uint16_t *vga_buf = larger_than_vga_buf + LINE_MARGIN * VGA_WIDTH;

	using cloudos::vga_buffer;
	using cloudos::vga_color;

	vga_buffer buf(vga_color::LIGHT_GREY, vga_color::BLACK, vga_buf, VGA_WIDTH, VGA_HEIGHT);

	SECTION( "when initializing vga_buffer, it is cleared, memory around it is not touched" ) {
		// after initialization, first two lines of buffer should still be set to
		// ALLOC_INIT; then, VGA_HEIGHT lines of correct init byte, then two more
		// lines of ALLOC_INIT
		uint16_t outside_line[VGA_WIDTH];
		memset(&outside_line[0], 0x3b, sizeof(outside_line));

		uint16_t inside_line[VGA_WIDTH];
		for(size_t x = 0; x < VGA_WIDTH; ++x) {
			// (LIGHT_GREY | BLACK << 4) << 8 | ' '
			inside_line[x] = 0x0720;
		}

		for(size_t y = 0; y < VGA_HEIGHT_ALLOC; ++y) {
			if(y < LINE_MARGIN || y >= VGA_HEIGHT_ALLOC - LINE_MARGIN) {
				REQUIRE(memcmp(larger_than_vga_buf + y * VGA_WIDTH, outside_line, sizeof(outside_line)) == 0);
			} else {
				REQUIRE(memcmp(larger_than_vga_buf + y * VGA_WIDTH, inside_line, sizeof(inside_line)) == 0);
			}
		}
	};

	SECTION( "setting and getting the mouse cursor" ) {
		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 0);
		REQUIRE(column == 0);

		buf.set_cursor(4, 7);

		buf.get_cursor(&row, &column);
		REQUIRE(row == 4);
		REQUIRE(column == 7);
	}

	SECTION( "setting and getting the color" ) {
		vga_color fg, bg;
		buf.get_color(&fg, &bg);
		REQUIRE(fg == vga_color::LIGHT_GREY);
		REQUIRE(bg == vga_color::BLACK);

		buf.set_color(vga_color::GREEN, vga_color::LIGHT_GREEN);
		buf.get_color(&fg, &bg);
		REQUIRE(fg == vga_color::GREEN);
		REQUIRE(bg == vga_color::LIGHT_GREEN);
	}

	SECTION( "writing a character" ) {
		buf.set_cursor(6, 9);

		buf.putc_at('f', vga_color::RED, vga_color::MAGENTA, 5, 8);
		uint16_t line[VGA_WIDTH];
		for(size_t x = 0; x < VGA_WIDTH; ++x) {
			// for x=5: (RED | MAGENTA << 4) << 8 | 'f'
			line[x] = x == 5 ? 0x5466 : 0x0720;
		}

		REQUIRE(memcmp(larger_than_vga_buf + (8 + LINE_MARGIN) * VGA_WIDTH, line, sizeof(line)) == 0);
		char c;
		vga_color fg, bg;
		buf.getc_at(&c, &fg, &bg, 5, 8);
		REQUIRE(c == 'f');
		REQUIRE(fg == vga_color::RED);
		REQUIRE(bg == vga_color::MAGENTA);

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 6);
		REQUIRE(column == 9);
	}

	SECTION( "writing newline" ) {
		buf.set_cursor(3, 5);
		buf.putc('\n');
		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 4);
		REQUIRE(column == 0);
	}

	SECTION( "writing carriage return" ) {
		buf.set_cursor(3, 5);
		buf.putc('\r');
		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 3);
		REQUIRE(column == 0);
	}

	SECTION( "writing a colored line" ) {
		buf.set_cursor(2, 6);
		buf.set_color(vga_color::LIGHT_CYAN, vga_color::WHITE);

		buf.write("Hello world!\rfoo\n");
		uint16_t line[VGA_WIDTH];
		for(size_t x = 0; x < VGA_WIDTH; ++x) {
			line[x] = 0x0720;
		}
		// (LIGHT_CYAN | WHITE << 4) << 8 | character
		memcpy(&line[0], "\x66\xfb\x6f\xfb\x6f\xfb" // foo
				"\x20\x07\x20\x07\x20\x07" // 3 spaces
				"\x48\xfb\x65\xfb\x6c\xfb\x6c\xfb" // Hell
				"\x6f\xfb\x20\xfb\x77\xfb\x6f\xfb" // o wo
				"\x72\xfb\x6c\xfb\x64\xfb\x21\xfb", // rld!
				36);

		REQUIRE(memcmp(larger_than_vga_buf + (2 + LINE_MARGIN) * VGA_WIDTH, line, sizeof(line)) == 0);

		size_t row, column;
		buf.get_cursor(&row, &column);
		REQUIRE(row == 3);
		REQUIRE(column == 0);
	}

	SECTION( "writing at end of buffer" ) {
		buf.write("Fall-off\n");
		buf.set_cursor(VGA_HEIGHT - 2, 0);
		buf.write("First\n");
		buf.write("Second\nThird");

		uint16_t outside_line[VGA_WIDTH];
		memset(&outside_line[0], 0x3b, sizeof(outside_line));

		uint16_t empty_line[VGA_WIDTH];
		uint16_t first[VGA_WIDTH];
		uint16_t second[VGA_WIDTH];
		uint16_t third[VGA_WIDTH];
		for(size_t x = 0; x < VGA_WIDTH; ++x) {
			// (LIGHT_GREY | BLACK << 4) << 8 | ' '
			empty_line[x] = 0x0720;
			first[x] = 0x0720;
			second[x] = 0x0720;
			third[x] = 0x0720;
		}
		memcpy(&first[0], "\x46\x07\x69\x07\x72\x07\x73\x07\x74\x07", 10);
		memcpy(&second[0], "\x53\x07\x65\x07\x63\x07\x6F\x07\x6E\x07\x64\x07", 12);
		memcpy(&third[0], "\x54\x07\x68\x07\x69\x07\x72\x07\x64\x07", 10);

		for(size_t y = 0; y < VGA_HEIGHT_ALLOC; ++y) {
			uint16_t *cmp;
			if(y < LINE_MARGIN || y >= VGA_HEIGHT_ALLOC - LINE_MARGIN) {
				cmp = outside_line;
			} else if(y == LINE_MARGIN + VGA_HEIGHT - 3) {
				cmp = first;
			} else if(y == LINE_MARGIN + VGA_HEIGHT - 2) {
				cmp = second;
			} else if(y == LINE_MARGIN + VGA_HEIGHT - 1) {
				cmp = third;
			} else {
				cmp = empty_line;
			}
			CHECK(memcmp(larger_than_vga_buf + y * VGA_WIDTH, cmp, sizeof(outside_line)) == 0);
		}
	}
}
