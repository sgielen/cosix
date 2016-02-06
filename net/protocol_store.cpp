#include "net/protocol_store.hpp"
#include "net/ethernet.hpp"
#include "net/ip.hpp"
#include "net/arp.hpp"
#include "net/icmp.hpp"
#include "net/udp.hpp"
#include "net/tcp.hpp"
#include "net/dhcp.hpp"
#include "memory/allocator.hpp"
#include "global.hpp"

using namespace cloudos;

protocol_store::protocol_store() {
	allocator *a = get_allocator();
#define REGISTER_PROTOCOL(protocol) \
	protocol = a->allocate<protocol##_implementation>(); \
	new (protocol) protocol##_implementation()
	REGISTER_PROTOCOL(ethernet);
	REGISTER_PROTOCOL(ip);
	REGISTER_PROTOCOL(icmp);
	REGISTER_PROTOCOL(arp);
	REGISTER_PROTOCOL(udp);
	REGISTER_PROTOCOL(tcp);

	dhcp = a->allocate<dhcp_client>();
	new (dhcp) dhcp_client();
}
