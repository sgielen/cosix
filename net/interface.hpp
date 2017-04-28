#pragma once

#include <stdint.h>
#include <stddef.h>
#include "oslibc/error.h"
#include "oslibc/list.hpp"
#include "memory/smart_ptr.hpp"

namespace cloudos {

struct interface_store;
struct rawsock;

typedef linked_list<weak_ptr<rawsock>> rawsock_list;

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
	enum hwtype_t {
		loopback,
		ethernet,
	};

	interface(hwtype_t);
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
	 * This method returns the interface hardware / lower level type.
	 */
	inline hwtype_t get_hwtype() {
		return hwtype;
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
	 * This method is used to send a frame to this interface, including the
	 * lower-level frame.
	 */
	virtual cloudabi_errno_t send_frame(uint8_t *packet, size_t length) = 0;

	/**
	 * This method is used to subscribe for incoming IP packets to this interface.
	 */
	void subscribe(weak_ptr<rawsock> sock);

protected:
	/**
	 * This method can be called when a frame is received on this interface.
	 */
	cloudabi_errno_t received_frame(uint8_t *frame, size_t frame_length);

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
	hwtype_t hwtype;
	char name[8];
	char mac[16];
	size_t mac_size = 0;
	rawsock_list *subscribed_sockets = nullptr;
};

}
