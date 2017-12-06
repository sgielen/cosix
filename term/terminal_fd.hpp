#pragma once
#include <fd/fd.hpp>
#include <term/terminal.hpp>
#include <memory/smart_ptr.hpp>

namespace cloudos {

struct terminal_fd : public fd_t {
	terminal_fd(shared_ptr<terminal>, cloudabi_fdflags_t f);
	terminal_fd(shared_ptr<terminal>, cloudabi_fdflags_t f, const char *n);

	~terminal_fd() override;

	size_t read(void *dest, size_t count) override;
	size_t write(const char *str, size_t count) override;

private:
	shared_ptr<terminal> term;
};

}
