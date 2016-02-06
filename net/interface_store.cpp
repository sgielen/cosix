#include "net/interface_store.hpp"
#include "net/interface.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/string.h"
#include "global.hpp"
#include "memory/allocator.hpp"

using namespace cloudos;

void cloudos::dump_interfaces(vga_stream &stream, interface_store *store)
{
	stream << "Interfaces:\n";
	for(interface_list *list = store->get_interfaces(); list; list = list->next) {
		stream << "* " << list->interface->get_name() << "\n";
		for(auto *ip_list = list->interface->get_ipv4addr_list(); ip_list; ip_list = ip_list->next) {
			uint8_t *a = ip_list->address;
			stream << "  IPv4: " << dec << a[0] << "." << a[1] << "." << a[2] << "." << a[3] << "\n";
		}
	}
}

interface_store::interface_store()
: interfaces_(nullptr)
{}

interface *interface_store::get_interface(const char *name)
{
	for(interface_list *list = interfaces_; list; list = list->next) {
		if(strcmp(list->interface->get_name(), name) == 0) {
			return list->interface;
		}
	}
	return nullptr;
}

error_t interface_store::register_interface(interface *i, const char *prefix)
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
	return error_t::file_exists;
}

error_t interface_store::register_interface_fixed_name(interface *i, const char *name)
{
	if(get_interface(name) != nullptr) {
		return error_t::file_exists;
	}

	i->set_name(name);

	interface_list *next_entry = get_allocator()->allocate<interface_list>();
	next_entry->interface = i;
	next_entry->next = nullptr;

	if(interfaces_ == nullptr) {
		interfaces_ = next_entry;
		return error_t::no_error;
	}

	interface_list *list = interfaces_;
	while(true) {
		if(list->next == nullptr) {
			list->next = next_entry;
			return error_t::no_error;
		}
		list = list->next;
	}
}
