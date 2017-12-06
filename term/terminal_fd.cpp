#include <term/terminal_fd.hpp>

using namespace cloudos;

terminal_fd::terminal_fd(shared_ptr<terminal> t, cloudabi_fdflags_t flags)
: terminal_fd(t, flags, nullptr)
{
}

terminal_fd::terminal_fd(shared_ptr<terminal> t, cloudabi_fdflags_t f, const char *n)
: fd_t(CLOUDABI_FILETYPE_CHARACTER_DEVICE, f, n ? n : "")
, term(t)
{
	if(n == nullptr) {
		strncpy(name, "terminal_fd to ", sizeof(name));
		strlcat(name, t->get_name(), sizeof(name));
	}
}

terminal_fd::~terminal_fd()
{}

size_t terminal_fd::read(void *dest, size_t count)
{
	error = term->read_keystrokes(reinterpret_cast<char*>(dest), &count);
	return error == 0 ? count : 0;
}

size_t terminal_fd::write(const char *str, size_t count)
{
	error = term->write_output(str, count);
	return error == 0 ? count : 0;
}
