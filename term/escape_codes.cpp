#include <oslibc/assert.hpp>
#include <oslibc/ctype.h>
#include <oslibc/string.h>
#include <stddef.h>
#include <stdint.h>
#include <term/escape_codes.hpp>

using namespace cloudos;

bool cloudos::next_token(char *input, size_t *inputsz, char *next_token, size_t *tokensz)
{
	if(*inputsz == 0 || *tokensz == 0) {
		return false;
	}

	size_t bytes_consumed = 0;
	// Consume UTF-8 characters until they no longer fit in the
	// token
	while(bytes_consumed < *tokensz
	&& bytes_consumed < *inputsz
	&& !is_control_character(input + bytes_consumed, 1)) {
		// There is still space for tokens, there is a byte to consume and it isn't a control character
		int unicode_length = unicode_character_length(input + bytes_consumed, *inputsz - bytes_consumed);
		assert(unicode_length >= -2);
		assert(unicode_length != 0);
		assert(unicode_length <= 4);
		if(unicode_length == -2) {
			// Not enough data available to complete this sequence.
			if(bytes_consumed > 0) {
				// Return the data we have now, we can return
				// this character when more bytes are
				// available.
				break;
			} else {
				// We have nothing to return.
				return false;
			}
		} else if(unicode_length == -1) {
			// It is an invalid sequence; we'll allow this data through as separate bytes because
			// what can you do
			if(bytes_consumed == 0) {
				// Let at least one character through so we can recover after this
				bytes_consumed = 1;
			}
			break;
		}
		int remaining_size = *tokensz - bytes_consumed;
		if(unicode_length > remaining_size) {
			// This Unicode character doesn't fit in the token array.
			if(bytes_consumed > 0) {
				// Just return what we have
				break;
			} else {
				// We have nothing to return.
				return false;
			}
		}
		// include this Unicode character in the token
		bytes_consumed += unicode_length;
	}

	if(bytes_consumed == 0) {
		assert(is_control_character(input, 1));
		if(is_escape_sequence(input, *inputsz)) {
			int escape_length = escape_sequence_length(input, *inputsz);
			assert(escape_length != 0);
			if(escape_length < 0) {
				// incomplete escape sequence
				return false;
			}
			if(static_cast<size_t>(escape_length) > *tokensz) {
				// it won't fit
				return false;
			}
			bytes_consumed = escape_length;
		} else {
			// return this control character
			bytes_consumed = 1;
		}
	}

	assert(bytes_consumed > 0);
	assert(bytes_consumed <= *tokensz);
	assert(bytes_consumed <= *inputsz);

	memcpy(next_token, input, bytes_consumed);
	*tokensz = bytes_consumed;

	memmove(input, input + bytes_consumed, *inputsz - bytes_consumed);
	*inputsz -= bytes_consumed;
	return true;
}

bool cloudos::is_control_character(char const *token, size_t sz)
{
	auto *tkn = reinterpret_cast<uint8_t const*>(token);
	return sz == 1 && (tkn[0] < 0x20 || tkn[0] == 127);
}

bool cloudos::is_escape_sequence(char const *token, size_t sz)
{
	return sz >= 1 && token[0] == 0x1b /* ESC */;
}

int cloudos::unicode_character_length(char const *token, size_t sz)
{
	if(sz == 0) {
		return 0;
	}
	if(token[0] == 0) {
		return 0;
	}
	bool first_bit  = (token[0] & 0x80) == 0x80;
	bool second_bit = (token[0] & 0x40) == 0x40;
	bool third_bit  = (token[0] & 0x20) == 0x20;
	bool fourth_bit = (token[0] & 0x10) == 0x10;
	bool fifth_bit  = (token[0] & 0x08) == 0x08;
	if(!first_bit) {
		return 1;
	}
	if(!second_bit) {
		// only valid for subsequent UTF-8 characters
		return -1;
	}
	int length = !third_bit ? 2 : (!fourth_bit ? 3 : (!fifth_bit ? 4 : -1));
	if(length == -1) {
		return -1;
	} else if(static_cast<int>(sz) < length) {
		return -2;
	}
	// check whether we actually have a valid sequence of this length
	for(int i = 1; i < length; ++i) {
		first_bit  = (token[i] & 0x80) == 0x80;
		second_bit = (token[i] & 0x40) == 0x40;
		if(!first_bit || second_bit) {
			// not a valid UTF-8 continuation
			return -1;
		}
	}
	return length;
}

int cloudos::escape_sequence_length(char const *token, size_t sz)
{
	if(sz == 0) {
		return 0;
	}
	if(token[0] != 0x1b) {
		return 0;
	}
	if(sz < 3) {
		return -1;
	}
	if(token[1] == 'O') {
		return sz < 3 ? -1 : 3;
	} else if(token[1] != '[') {
		// unknown type
		return 2;
	}
	// CSI = ^[[
	// ^[[C or ^[[NC or ^[[N;NC or ^[[?NC
	size_t length = 3;
	int array = 0;
	while(1) {
		if(length > sz) {
			return -1;
		}
		char c = token[length-1];
		if(isdigit(c)) {
			length++;
		} else if(c == ';') {
			length++;
			array++;
		} else if(length == 3 && c == '?') {
			length++;
		} else if(isalpha(c) || c == '~' || c == '$' || c == '^' || c == '@') {
			return length;
		} else {
			// unknown type
			return length - 1;
		}
	}
}

bool cloudos::is_escape_code_terminal_size(const char *token, size_t sz)
{
	// \x1b [ 8 ; <height> ; <width> t
	if(sz < 8) {
		return false;
	}
	if(strncmp(token, "\x1b[8;", 4) != 0) {
		return false;
	}
	if(token[sz - 1] != 't') {
		return false;
	}
	return true;
}

bool cloudos::is_escape_code_move_cursor(const char *token, size_t sz)
{
	// \x1b [ <x> ; <y> H
	if(sz < 6) {
		return false;
	}
	if(strncmp(token, "\x1b[", 2) != 0) {
		return false;
	}
	if(token[sz - 1] != 'H') {
		return false;
	}
	return true;
}

#define CMP_ESC(name, str) \
bool cloudos::is_escape_code_##name(const char *token, size_t sz) \
{ \
	return sz == strlen(str) && strncmp(token, str, sz) == 0; \
}

CMP_ESC(get_terminal_size, "\x1b[18t")
CMP_ESC(terminal_size_changed, "\x1b[?8000h")
CMP_ESC(echoing_off, "\x1b[?8001h")
CMP_ESC(echoing_on, "\x1b[?8002h")
CMP_ESC(crlf_off, "\x1b[?8003h")
CMP_ESC(crlf_on, "\x1b[?8004h")
CMP_ESC(cursor_visible, "\x1b[?25h")
CMP_ESC(cursor_invisible, "\x1b[?25l")

#undef CMP_ESC
