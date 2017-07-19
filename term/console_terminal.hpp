#pragma once

#include <term/terminal.hpp>
#include <fd/fd.hpp>

namespace cloudos {

struct console_terminal : public terminal_impl {
	console_terminal();

	cloudabi_errno_t write_output_token(const char *token, size_t tokensz) override;
};

}
