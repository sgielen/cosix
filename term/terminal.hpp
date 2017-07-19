#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cloudabi_types.h>

namespace cloudos {

/**
 * A terminal is a common way for a user to interact with the operating system.
 * Terminals can be made available on the VGA interface or serial ports, as
 * well as exposed to the userspace, in which case they are called
 * pseudoterminals. Such pseudoterminals can be used, for example, to expose
 * terminal characteristics over telnet or SSH connections, graphical terminals
 * or multiplexers.
 *
 * A terminal has a size, a cursor location, and some flags, like whether the
 * cursor should be visible and whether echoing is enabled. These properties can
 * be controlled by writing ANSI escape codes to it. Some of those escape codes
 * are specific to Cosix.
 */
struct terminal {
	virtual ~terminal();

	virtual const char *get_name() = 0;
	virtual cloudabi_errno_t write_output(const char *data, size_t length) = 0;
	virtual cloudabi_errno_t write_keystrokes(const char *data, size_t length) = 0;
	virtual cloudabi_errno_t read_keystrokes(char *data, size_t *length) = 0;
};

/**
 * A utility implementation of terminal that already helps a lot.
 */
struct terminal_impl : public terminal {
	terminal_impl(const char *name);

	const char *get_name() final override;
	cloudabi_errno_t write_output(const char *data, size_t length) final override;
	cloudabi_errno_t write_keystrokes(const char *data, size_t length) override;
	cloudabi_errno_t read_keystrokes(char *data, size_t *length) override;

protected:
	// Inside write_output_token, do not call write_keystrokes or write_output as the
	// new output may appear in the middle of existing output. Instead, call queue_output,
	// which will queue up the characters until the current write is complete.
	virtual cloudabi_errno_t write_output_token(const char *token, size_t tokensz) = 0;

	// Only call this method in write_output_token.
	void queue_keystrokes_during_write(const char *input, size_t inputsz);

private:
	// if true, do not send the escape code further upstream, e.g. because it is
	// Cosix proprietary
	bool inner_handle_escape_code(const char *token, size_t tokensz);

	char name[32];

	char output_buffer[64];
	size_t output_buffer_used;

	char keystroke_buffer[128];
	size_t keystroke_buffer_used = 0;

	char queued_keystrokes_buffer[128];
	size_t queued_keystrokes_buffer_used = 0;

	bool echoing = true;
	bool lf_to_crlf = true;
};

}
