#include "net/interface.hpp"
#include "net/interface_store.hpp"
#include "global.hpp"
#include "oslibc/string.h"
#include "oslibc/in.h"
#include "memory/allocator.hpp"
#include "net/udp.hpp"
#include "fd/rawsock.hpp"

using namespace cloudos;

interface::interface(hwtype_t h)
: hwtype(h)
, ipv4_addrs(nullptr)
, ipv6_addrs(nullptr)
{
	name[0] = 0;
}

cloudabi_errno_t interface::received_ip_packet(uint8_t *frame, size_t frame_length, protocol_t type, size_t ip_hdr_offset)
{
	// remove all weak pointers that are expired
	remove_all(&subscribed_sockets, [&](rawsock_list *item) {
		return !item->data.lock().is_initialized();
	});

	// send message to all remaining weak pointers
	iterate(subscribed_sockets, [&](rawsock_list *item) {
		auto shared = item->data.lock();
		if(shared) {
			shared->interface_recv(frame, frame_length, type, ip_hdr_offset);
		}
	});
	
	// handle it in-kernel as well for now
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

void interface::subscribe(weak_ptr<rawsock> sock)
{
	rawsock_list *item = allocate<rawsock_list>(sock);
	append(&subscribed_sockets, item);
}
