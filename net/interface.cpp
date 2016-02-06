#include "net/interface.hpp"
#include "net/interface_store.hpp"
#include "global.hpp"
#include "oslibc/string.h"
#include "oslibc/in.h"
#include "memory/allocator.hpp"
#include "net/udp.hpp"

using namespace cloudos;

interface::interface()
: ipv4_addrs(nullptr)
, ipv6_addrs(nullptr)
{
	name[0] = 0;
}

error_t interface::received_ip_packet(uint8_t *frame, size_t frame_length, protocol_t, size_t ip_hdr_offset)
{
	return get_protocol_store()->ip->received_ip_packet(this, frame + ip_hdr_offset, frame_length - ip_hdr_offset);
}

void interface::set_name(const char *n)
{
	memcpy(name, n, sizeof(name));
}

error_t interface::add_ipv4_addr(uint8_t const ip[4])
{
	ipv4addr_list *new_entry = get_allocator()->allocate<ipv4addr_list>();
	memcpy(new_entry->address, ip, 4);
	new_entry->next = nullptr;

	if(ipv4_addrs == nullptr) {
		ipv4_addrs = new_entry;
		return error_t::no_error;
	}

	auto *list = ipv4_addrs;
	for(; list->next == nullptr; list = list->next) {
		if(memcmp(list->address, ip, 4)) {
			return error_t::file_exists;
		}
	}
	list->next = new_entry;
	return error_t::no_error;
}
