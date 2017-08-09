#include <cassert>
#include <errno.h>
#include <sstream>
#include <unistd.h>

#include "../../term/escape_codes.hpp"
#include "terminal.hpp"

using namespace cloudos;
using namespace cosix::irc;

static bool next_token(std::string &input, std::string &token) {
	size_t isz = input.size();
	char i[isz];
	memcpy(i, input.c_str(), isz);

	char tkn[64];
	size_t tokensz = sizeof(tkn);

	bool res = next_token(i, &isz, tkn, &tokensz);
	input = std::string(i, isz);
	token = std::string(tkn, tokensz);
	return res;
}

terminal::terminal(cloudabi_fd_t t)
: fd(t)
{}

std::pair<size_t, size_t> terminal::size() {
	return std::make_pair(width, height);
}

std::pair<size_t, size_t> terminal::predict_size_of_input(std::string input) {
	auto sz = size();

	size_t w = 0;
	size_t h = 0;

	std::string token;
	while(next_token(input, token)) {
		if(is_escape_sequence(token.c_str(), token.size())) {
			// these aren't printed; it may influence the size of
			// the output if it's a cursor relocation but that's
			// unsupported by this function
		} else if(is_control_character(token.c_str(), token.size())) {
			if(token[0] == '\n') {
				if(h == 0) h++;
				w = 0;
				h++;
			} // else, assume it's zerowidth
		} else {
			// normal printable characters; figure out how many.
			// next_token takes care of completing UTF-8 sequences
			// for us, so we can assume we have only complete
			// sequences (or invalid ones)
			for(size_t n = 0; n < token.size();) {
				int length = unicode_character_length(token.c_str() + n, token.size() - n);
				assert(length >= 1);
				if(h == 0) h++;
				if(w++ == sz.first) {
					w = 0;
					h++;
				}
				n += length;
			}
		}
	}
	return std::make_pair(w, h);
}

void terminal::enable_cursor_display() {
	write("\e[?25h");
}

void terminal::disable_cursor_display() {
	write("\e[?25l");
}

void terminal::enable_echoing() {
	write("\e[?8002h");
}

void terminal::disable_echoing() {
	write("\e[?8001h");
}

void terminal::clear_screen() {
	write("\e[2J\e[;H");
}

void terminal::set_cursor(size_t x, size_t y) {
	write("\e[" + std::to_string(x - 1) + ";" + std::to_string(y - 1) + "H");
}

void terminal::write(std::string buf) {
	while(!buf.empty()) {
		ssize_t written = ::write(fd, buf.c_str(), buf.size());
		if(written == -1) {
			throw std::runtime_error("Failed to write to terminal: " + std::string(strerror(errno)));
		}
		buf = buf.substr(written);
	}
}

void terminal::request_size() {
	write("\e[18t");
}

void terminal::wait_for_size() {
	while(1) {
		for(size_t i = 0; i < tokens.size(); ++i) {
			if(is_escape_code_terminal_size(tokens[i].c_str(), tokens[i].size())) {
				return;
			}
		}
		// try reading more data into the tokenbuf
		read();
	}
	
}

std::string terminal::get_token() {
	while(tokens.empty()) {
		read();
	}
	std::string res = tokens.front();
	tokens.pop_front();
	return res;
}

void terminal::read() {
	char buf[128];
	ssize_t r = ::read(fd, buf, sizeof(buf));
	if(r < 0) {
		throw std::runtime_error("Failed to read to terminal: " + std::string(strerror(errno)));
	}
	tokenbuf.append(buf, r);

	// now, read tokenbuf into tokens
	bool request_sz = false;
	std::string token;
	while(next_token(tokenbuf, token)) {
		if(is_escape_code_terminal_size(token.c_str(), token.size())) {
			std::stringstream h_w;
			h_w << token.substr(4);
			h_w >> height;
			char semi;
			h_w >> semi;
			h_w >> width;
			// 1-indexed to 0-indexed
			height--;
			width--;
			request_sz = false;
		} else if(is_escape_code_terminal_size_changed(token.c_str(), token.size())) {
			request_sz = true;
		}
		tokens.push_back(token);
	}

	if(request_sz) {
		// a terminal size change came in without being answered yet, request correct size
		request_size();
	}
}
