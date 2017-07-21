#include <oslibc/assert.hpp>
#include <oslibc/string.h>
#include <term/escape_codes.hpp>
#include <term/terminal.hpp>

using namespace cloudos;

terminal::~terminal()
{}

const char *terminal_impl::get_name()
{
	return name;
}

terminal_impl::terminal_impl(const char *n)
{
	strncpy(name, n, sizeof(name));
	name[sizeof(name)-1] = 0;
}

cloudabi_errno_t terminal_impl::write_output(const char *data, size_t length) {
	assert(queued_keystrokes_buffer_used == 0);
	while(length > 0) {
		// append data to output buffer
		size_t copy_to_buffer = sizeof(output_buffer) - output_buffer_used;
		if(copy_to_buffer > length) {
			copy_to_buffer = length;
		}

		memcpy(output_buffer + output_buffer_used, data, copy_to_buffer);
		output_buffer_used += copy_to_buffer;
		data += copy_to_buffer;
		length -= copy_to_buffer;

		char token[32];
		size_t token_size = sizeof(token);
		while(next_token(output_buffer, &output_buffer_used, token, &token_size)) {
			assert(token_size > 0);
			assert(token_size <= sizeof(token));
			if(!inner_handle_escape_code(token, token_size)) {
				if(token_size == 1 && token[0] == '\n' && lf_to_crlf) {
					// send CR first
					auto res = write_output_token("\r", 1);
					if(res != 0) {
						return res;
					}
				}
				auto res = write_output_token(token, token_size);
				if(res != 0) {
					return res;
				}
			}
		}
	}
	if(queued_keystrokes_buffer_used > 0) {
		// the terminal has responded to this output, send the response as new keystrokes
		char q[sizeof(queued_keystrokes_buffer)];
		size_t size = queued_keystrokes_buffer_used;
		memcpy(q, queued_keystrokes_buffer, size);

		queued_keystrokes_buffer_used = 0;
		write_keystrokes(q, size);
	}
	return 0;
}

// Only call this method in write_output_token.
void terminal_impl::queue_keystrokes_during_write(const char *input, size_t inputsz)
{
	size_t copy_to_buffer = sizeof(queued_keystrokes_buffer) - queued_keystrokes_buffer_used;
	if(copy_to_buffer > inputsz) {
		copy_to_buffer = inputsz;
	}
	memcpy(queued_keystrokes_buffer + queued_keystrokes_buffer_used, input, copy_to_buffer);
	queued_keystrokes_buffer_used += copy_to_buffer;
}

cloudabi_errno_t terminal_impl::write_keystrokes(const char *data, size_t length) {
	size_t copy_to_buffer = sizeof(keystroke_buffer) - keystroke_buffer_used;
	if(copy_to_buffer > length) {
		copy_to_buffer = length;
	}
	memcpy(keystroke_buffer + keystroke_buffer_used, data, copy_to_buffer);
	keystroke_buffer_used += copy_to_buffer;

	read_cv.broadcast();

	if(echoing) {
		// TODO: what if, somewhere in the middle, there is a keystroke that
		// should turn off echoing? It's unlikely, since it will usually be
		// applications writing it, but could happen.
		// Also, write output *after* appending to the buffer, since write_output
		// may cause automatic keystrokes which need to be *after* this input.
		return write_output(data, length);
	}

	return 0;
}

cloudabi_errno_t terminal_impl::read_keystrokes(char *data, size_t *length) {
	while(keystroke_buffer_used == 0) {
		read_cv.wait();
	}

	if(*length > keystroke_buffer_used) {
		*length = keystroke_buffer_used;
	}

	memcpy(data, keystroke_buffer, *length);
	memmove(keystroke_buffer, keystroke_buffer + *length, sizeof(keystroke_buffer) - *length);
	keystroke_buffer_used -= *length;
	return 0;
}

bool terminal_impl::inner_handle_escape_code(const char *token, size_t tokensz)
{
	// Don't use else {} in this function, because it might be possible
	// that we extend escape codes so that one escape code can make
	// multiple changes
#define IS(type) is_escape_code_ ##type (token, tokensz)
	if(IS(echoing_off)) {
		echoing = false;
	}
	if(IS(echoing_on)) {
		echoing = true;
	}
	if(IS(crlf_off)) {
		lf_to_crlf = false;
	}
	if(IS(crlf_on)) {
		lf_to_crlf = true;
	}
	return false; // for now, always send them onwards as well
#undef IS
}
