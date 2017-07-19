#include <memory/allocation.hpp>
#include <oslibc/error.h>
#include <oslibc/string.h>
#include <term/terminal.hpp>
#include <term/terminal_store.hpp>

using namespace cloudos;

terminal_store::terminal_store()
: terminals_(nullptr)
{}

terminal *terminal_store::get_terminal(const char *name)
{
	terminal_list *found = find(terminals_, [name](terminal_list *item) {
		return strcmp(item->data->get_name(), name) == 0;
	});
	return found == nullptr ? nullptr : found->data;
}

cloudabi_errno_t terminal_store::register_terminal(terminal *i)
{
	if(get_terminal(i->get_name()) != nullptr) {
		return EEXIST;
	}

	terminal_list *next_entry = allocate<terminal_list>(i);
	append(&terminals_, next_entry);
	return 0;
}

