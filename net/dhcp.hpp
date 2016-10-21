#pragma once

#include "net/udp.hpp"
#include "oslibc/error.h"

namespace cloudos {

struct ethernet_interface;

struct dhcp_client : public udp_listener {
	dhcp_client();

	cloudabi_errno_t start_dhcp_discover_for(ethernet_interface *iface);
	cloudabi_errno_t received_udp4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t source, uint16_t sourceport, ipv4addr_t destination, uint16_t destport) override;

private:
	cloudabi_errno_t send_dhcp_request(interface *iface, uint8_t *old_payload);

	bool discovering;
	bool requesting;
	uint8_t active_xid[4];
};

}
