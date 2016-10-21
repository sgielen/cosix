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

cloudabi_errno_t interface::received_ip_packet(uint8_t *frame, size_t frame_length, protocol_t, size_t ip_hdr_offset)
{
	return get_protocol_store()->ip->received_ip_packet(this, frame + ip_hdr_offset, frame_length - ip_hdr_offset);
}

void interface::set_name(const char *n)
{
	memcpy(name, n, sizeof(name));
}

cloudabi_errno_t interface::add_ipv4_addr(uint8_t const ip[4])
{
	if(contains(ipv4_addrs, [&ip](ipv4addr_list const *item){
	    return memcmp(item->data, ip, 4) == 0;
	})) {
		return EEXIST;
	}

	ipv4addr_list *new_entry = get_allocator()->allocate<ipv4addr_list>();
	memcpy(new_entry->data, ip, 4);
	new_entry->next = nullptr;

	append(&ipv4_addrs, new_entry);
	return 0;
}
