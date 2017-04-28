#include "net/interface.hpp"
#include "net/interface_store.hpp"
#include "global.hpp"
#include "oslibc/string.h"
#include "oslibc/in.h"
#include "memory/allocator.hpp"
#include "fd/rawsock.hpp"

using namespace cloudos;

interface::interface(hwtype_t h)
: hwtype(h)
{
	name[0] = 0;
}

cloudabi_errno_t interface::received_frame(uint8_t *frame, size_t frame_length)
{
	// remove all weak pointers that are expired
	remove_all(&subscribed_sockets, [&](rawsock_list *item) {
		return !item->data.lock().is_initialized();
	});

	// send message to all remaining weak pointers
	iterate(subscribed_sockets, [&](rawsock_list *item) {
		auto shared = item->data.lock();
		if(shared) {
			shared->frame_received(frame, frame_length);
		}
	});

	return 0;
}

void interface::set_name(const char *n)
{
	memcpy(name, n, sizeof(name));
}

void interface::subscribe(weak_ptr<rawsock> sock)
{
	rawsock_list *item = allocate<rawsock_list>(sock);
	append(&subscribed_sockets, item);
}
