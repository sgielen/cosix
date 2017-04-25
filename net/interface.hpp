#pragma once

#include <stdint.h>
#include <stddef.h>
#include "net/ip.hpp"
#include "net/protocol_store.hpp"
#include "oslibc/error.h"
#include "oslibc/list.hpp"

namespace cloudos {

struct interface_store;

typedef linked_list<ipv4addr_t> ipv4addr_list;
typedef linked_list<ipv6addr_t> ipv6addr_list;

/**
 * An interface is used for IP communication. It is an abstract concept with
 * implementations like the loopback_interface and the ethernet_interface. An
 * interface has a unique name in the system, has zero or more IPv4 addresses
 * and zero or more IPv6 addresses.
 *
 * Normally, the IP stack decides which interface to use for a transmission
 * using the route table, and it uses the interface_store to find a specific
 * interface.
 */
struct interface {
	interface();
	virtual ~interface() {}

	/**
	 * This method returns the interfaces unique name. This pointer is
	 * owned by this interface, and must not be freed. (If the interface is
	 * still being constructed, this method returns the empty string.)
	 */
	inline const char *get_name() {
		return name;
	}

	/**
	 * On interfaces that use it, this returns the MAC of the interface.
	 * The size of the buffer is returned using the parameter. The returned
	 * pointer is owned by the interface, and must never be freed by the
	 * caller. The value may become invalid or dangling after subsequent
	 * calls to methods of this interface.
	 */
	inline const char *get_mac(size_t *size) {
		*size = mac_size;
		return mac;
	}

	/**
	 * This method returns the list of IPv4 addresses on this interface. It
	 * returns nullptr if this interface has no IPv4 addresses. The
	 * returned pointer is owned by the interface, and must never be freed
	 * by the caller. The value may become invalid or dangling after
	 * subsequent calls to methods of this interface.
	 */
	inline ipv4addr_list *get_ipv4addr_list() {
		return ipv4_addrs;
	}

	/**
	 * This method returns the list of IPv6 addresses in this interface. It
	 * returns nullptr if this interface has no IPv6 addresses. The
	 * returned pointer is owned by the interface, and must never be freed
	 * by the caller. The value may become invalid or dangling after
	 * subsequent calls to methods of this interface.
	 */
	inline ipv6addr_list *get_ipv6addr_list() {
		return ipv6_addrs;
	}

	/**
	 * This method is used to send an IP packet to this interface.
	 */
	virtual cloudabi_errno_t send_packet(uint8_t *packet, size_t length) = 0;

	cloudabi_errno_t add_ipv4_addr(uint8_t const ip[4]);

protected:
	/**
	 * This method can be called when an IP packet is received on this interface.
	 */
	cloudabi_errno_t received_ip_packet(uint8_t *frame, size_t frame_length, protocol_t frame_type, size_t ip_hdr_offset);

	/**
	 * This method can be called to set the MAC address.
	 */
	void set_mac(const char *m, size_t s) {
		mac_size = s;
		if(mac_size > sizeof(mac)) {
			mac_size = sizeof(mac);
		}
		for(size_t i = 0; i < mac_size; ++i) {
			mac[i] = m[i];
		}
	}

private:
	void set_name(const char *name);

	friend struct interface_store;
	char name[8];
	ipv4addr_list *ipv4_addrs;
	ipv6addr_list *ipv6_addrs;
	char mac[16];
	size_t mac_size = 0;
};

}
