#include "net/interface_store.hpp"
#include "net/interface.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/string.h"
#include "global.hpp"

using namespace cloudos;

void cloudos::dump_interfaces(vga_stream &stream, interface_store *store)
{
	stream << "Interfaces:\n";
	iterate(store->get_interfaces(), [&stream](interface_list *item){
		stream << "* " << item->data->get_name() << "\n";
	});
}

interface_store::interface_store()
: interfaces_(nullptr)
{}

interface *interface_store::get_interface(const char *name)
{
	interface_list *found = find(interfaces_, [name](interface_list *item) {
		return strcmp(item->data->get_name(), name) == 0;
	});
	return found == nullptr ? nullptr : found->data;
}

cloudabi_errno_t interface_store::register_interface(interface *i, const char *prefix)
{
	// TODO: do this using printf
	char name[8];
	size_t prefixlen = strlen(prefix);
	if(prefixlen > 6) {
		prefixlen = 6;
	}
	memcpy(name, prefix, prefixlen);

	uint8_t suffix = 0;
	while(suffix < 10) {
		name[prefixlen] = 0x30 + suffix;
		name[prefixlen + 1] = 0;
		if(get_interface(name) == nullptr) {
			return register_interface_fixed_name(i, name);
		}
		suffix++;
	}
	return EEXIST;
}

cloudabi_errno_t interface_store::register_interface_fixed_name(interface *i, const char *name)
{
	if(get_interface(name) != nullptr) {
		return EEXIST;
	}

	i->set_name(name);

	interface_list *next_entry = allocate<interface_list>(i);
	append(&interfaces_, next_entry);
	return 0;
}
