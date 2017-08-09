#pragma once

#include <cloudabi_types.h>
#include <utility>
#include <string>
#include <deque>

namespace cosix {
namespace irc {

/*
Terminal responsibilities:
* Read from the terminal fd
  Using the same code as the kernel uses, turn data into tokens
* Keep a cache of how big the terminal is
  Initially, send the "how big is terminal?" escape code, then
  continuously read all terminal size codes, even gratituous
* Read escape codes like ^C, ^L
  Turn them into calls to a callback, e.g. "ctrl-c" or
    ('c', true, false, false)
* "where would the cursor position be after printing this line?"
  e.g. lines & column does it take to draw this text
*/
struct terminal {
	terminal(cloudabi_fd_t t);

	void enable_echoing();
	void disable_echoing();

	// Clear screen and move cursor to (0,0)
	void clear_screen();

	void enable_cursor_display();
	void disable_cursor_display();

	// (0, 0) is top left of screen
	void set_cursor(size_t x, size_t y);

	void request_size();
	void wait_for_size();
	std::pair<size_t, size_t> size();

	std::pair<size_t, size_t> predict_size_of_input(std::string input);

	std::string get_token();
	void write(std::string);

private:
	void read();
	std::deque<std::string> tokens;
	std::string tokenbuf;

	cloudabi_fd_t fd;
	size_t width = 0;
	size_t height = 0;
};

}
}
