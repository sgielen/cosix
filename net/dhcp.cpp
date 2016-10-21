#include "net/dhcp.hpp"
#include "net/interface_store.hpp"
#include "net/interface.hpp"
#include "net/ethernet_interface.hpp"
#include "hw/vga_stream.hpp"
#include "global.hpp"
#include "memory/allocator.hpp"
#include "oslibc/string.h"

using namespace cloudos;

const int DHCPDISCOVER = 1;
const int DHCPOFFER = 2;
const int DHCPREQUEST = 3;
/* const int DHCPDECLINE = 4; */
const int DHCPACK = 5;
/* const int DHCPNAK = 6; */
/* const int DHCPRELEASE = 7; */

const char * const dhcpmagic = "\x63\x82\x53\x63";

dhcp_client::dhcp_client()
: discovering(false)
, requesting(false)
{
	active_xid[0] = 0;
	active_xid[1] = 0;
	active_xid[2] = 0;
	active_xid[3] = 0;
}

cloudabi_errno_t dhcp_client::start_dhcp_discover_for(ethernet_interface *iface)
{
	uint8_t mac[6];
	auto res = iface->get_mac_address(reinterpret_cast<char*>(mac));
	if(res != 0) {
		return res;
	}

	// TODO: use a random starting XID
	memcpy(active_xid, mac + 2, 4);

	// TODO: this should probably be less hardcoded
	uint8_t bootp[] = {
		/* bootp */
		0x01, 0x01, 0x06, 0x00, // bootp request, ethernet, hw address length 6, 0 hops
		active_xid[0], active_xid[1], active_xid[2], active_xid[3], // id
		0x00, 0x00, 0x00, 0x00, // no time elapsed, no flags
		0x00, 0x00, 0x00, 0x00, // client IP
		0x00, 0x00, 0x00, 0x00, // "your" IP
		0x00, 0x00, 0x00, 0x00, // next server IP
		0x00, 0x00, 0x00, 0x00, // relay IP,
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // client MAC
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // hardware address padding
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		/* dhcp */
		0x63, 0x82, 0x53, 0x63, // dhcp magic cookie
		0x35, 0x01, DHCPDISCOVER, // dhcp request
		0x37, 0x0a, 0x01, 0x79, 0x03, 0x06, 0x0f, 0x77, 0xf, 0x5f, 0x2c, 0x2e, // request various other parameters as well
		0x39, 0x02, 0x05, 0xdc, // max response size
		0x3d, 0x07, 0x01, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // client identifier
		/* missing: requested IP address */
		/* missing: IP address lease time */
		0x0c, 0x07, 'c', 'l', 'o', 'u', 'd', 'o', 's', // hostname
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // end + padding
	};

	ipv4addr_t source, destination;
	source[0] = source[1] = source[2] = source[3] = 0;
	destination[0] = destination[1] = destination[2] = destination[3] = 0xff;

	// Send the initial DHCP discover
	// TODO: if we didn't get an offer after a while, we should send a new one
	discovering = true;
	return get_protocol_store()->udp->send_ipv4_udp(bootp, sizeof(bootp), source, 68, destination, 67);
}

cloudabi_errno_t dhcp_client::received_udp4(interface *iface, uint8_t *payload, size_t length, ipv4addr_t, uint16_t, ipv4addr_t, uint16_t)
{
	/* minimal BOOTP length is 236, and DHCP also describes a 4-byte magic, so 240 */
	if(length < 240) {
		get_vga_stream() << "  It's a too small BOOTP message, dropping\n";
		return 0;
	}

	uint8_t operation = payload[0];
	if(operation != 2 /* response */) {
		get_vga_stream() << "  It's a BOOTP message that's not a response, dropping\n";
		return 0;
	}

	uint8_t htype = payload[1];
	uint8_t hlen = payload[2];
	if(htype != 0x01 || hlen != 0x06) {
		get_vga_stream() << "  It's a BOOTP message with an unknown HTYPE, dropping\n";
		return 0;
	}

	const uint8_t *xid = payload + 4;
	if(memcmp(xid, active_xid, 4) != 0) {
		get_vga_stream() << "  It's a BOOTP message with an incorrect XID, dropping\n";
		return 0;
	}

	if(memcmp(payload + 236, dhcpmagic, 4) != 0) {
		get_vga_stream() << "  It's a BOOTP message without the DHCP magic, dropping\n";
		return 0;
	}

	const uint8_t *options = payload + 240;
	size_t options_length = length - 240;

	// parse incoming options
	uint8_t dhcp_type = 0;

	while(options_length > 2) {
		uint8_t option = options[0];
		if(option == 0xff) {
			break;
		}

		uint8_t option_length = options[1];
		if(options_length - 2 < option_length) {
			get_vga_stream() << "  It's a DHCP message that ends early, dropping\n";
			return 0;
		}
		if(option == 0x35 && option_length == 1) {
			dhcp_type = options[2];
		}

		options += option_length + 2;
		options_length -= option_length + 2;
	}

	if(dhcp_type == 0) {
		get_vga_stream() << "  It's a DHCP message without a (valid) message type, dropping\n";
		return 0;
	}

	uint8_t *ciaddr = payload + 12;
	uint8_t *yiaddr = payload + 16;
	uint8_t *siaddr = payload + 20;
	uint8_t *giaddr = payload + 24;

	if(dhcp_type == DHCPOFFER) {
		get_vga_stream() << "  It's a DHCP offer for the following IPs:\n";
	} else if(dhcp_type == DHCPACK) {
		get_vga_stream() << "  It's a DHCP acknowledgement for the following IPs:\n";
	} else {
		get_vga_stream() << "  It's an unexpected DHCP message type " << dhcp_type << ", dropping\n";
		return 0;
	}

	get_vga_stream() << "  ciaddr: " << ciaddr[0] << "." << ciaddr[1] << "." << ciaddr[2] << "." << ciaddr[3] << "\n";
	get_vga_stream() << "  yiaddr: " << yiaddr[0] << "." << yiaddr[1] << "." << yiaddr[2] << "." << yiaddr[3] << "\n";
	get_vga_stream() << "  siaddr: " << siaddr[0] << "." << siaddr[1] << "." << siaddr[2] << "." << siaddr[3] << "\n";
	get_vga_stream() << "  giaddr: " << giaddr[0] << "." << giaddr[1] << "." << giaddr[2] << "." << giaddr[3] << "\n";

	if(dhcp_type == DHCPOFFER && discovering) {
		// Currently, we just accept the very first offer we get. This
		// is fine for now, but per standard we should await all offers
		// for a while and pick the best one.
		discovering = false;
		requesting = true;
		return send_dhcp_request(iface, payload);
	} else if(dhcp_type == DHCPOFFER) {
		get_vga_stream() << "  ...But I'm not accepting offers anymore, so dropping.\n";
		return 0;
	} else if(dhcp_type == DHCPACK && requesting) {
		// TODO: when receiving DHCPACK, we /should/ send ARP request per rfc
		// 2131 4.4.1 and verify that the address is not used yet. If it is
		// used, send DHCPDECLINE and try a DHCPOFFER with another address. If
		// it is unused, broadcast an ARP reply with new IP address.
		// TODO: we should also take the timer information from the
		// offer/request/ack and ensure that we request a new lease as
		// soon as the timer expires.
		get_vga_stream() << "  DHCP request was accepted! Assigning IP address.\n";
		requesting = false;
		cloudabi_errno_t res = iface->add_ipv4_addr(yiaddr);
		if(res != 0) {
			get_vga_stream() << "  Failed to assign IPv4 address to device\n";
			return res;
		}

		dump_interfaces(get_vga_stream(), get_interface_store());
		return 0;
	} else if(dhcp_type == DHCPACK) {
		get_vga_stream() << "  ...But I wasn't waiting for an ACK at the moment, so dropping.\n";
		return 0;
	}

	// unreachable
	return 0;
}

cloudabi_errno_t dhcp_client::send_dhcp_request(interface *iface, uint8_t *old_payload)
{
	size_t dhcp_length = 250;
	uint8_t *dhcp = reinterpret_cast<uint8_t*>(get_allocator()->allocate(dhcp_length));
	dhcp[0] = 1 /* request */;
	dhcp[1] = 1 /* ethernet */;
	dhcp[2] = 6 /* ethernet hardware addresses */;
	dhcp[3] = 0 /* hops */;
	memcpy(dhcp + 4, old_payload + 4, 4); // xid
	dhcp[8] = 0 /* seconds */;
	dhcp[9] = 0 /* seconds */;
	dhcp[10] = 0 /* flags */;
	dhcp[11] = 0 /* flags */;
	dhcp[12] = dhcp[13] = dhcp[14] = dhcp[15] = 0 /* ciaddr */;
	dhcp[16] = dhcp[17] = dhcp[18] = dhcp[19] = 0 /* yiaddr */;
	memcpy(dhcp + 20, old_payload + 20, 8); // siaddr and giaddr
	// TODO: in the future, this may not be necessarily safe to do
	ethernet_interface *eth = reinterpret_cast<ethernet_interface*>(iface);
	char mac[6];
	auto res = eth->get_mac_address(mac);
	if(res != 0) {
		return res;
	}
	memcpy(dhcp + 28, mac, 6);
	for(size_t i = 34; i < 236; ++i) {
		dhcp[i] = 0; // client hardware address, server hostname, boot filename
	}
	dhcp[236] = 0x63; // DHCP magic cookie
	dhcp[237] = 0x82;
	dhcp[238] = 0x53;
	dhcp[239] = 0x63;

	dhcp[240] = 0x35; // DHCP Message Type
	dhcp[241] = 0x01;
	dhcp[242] = DHCPREQUEST; // DHCP Request

	dhcp[243] = 0x32; // Requested IP Address
	dhcp[244] = 0x04;
	uint8_t *yiaddr = old_payload + 16;
	memcpy(dhcp + 245, yiaddr, 4);

	dhcp[249] = 0xff; // End of options

	ipv4addr_t udp_source, udp_dest;
	udp_source[0] = udp_source[1] = udp_source[2] = udp_source[3] = 0;
	udp_dest[0] = udp_dest[1] = udp_dest[2] = udp_dest[3] = 0xff;
	// this currently fails in error_t virtio_net_device::send_ethernet_frame(uint8_t *frame, size_t length)
	// because it is only able to send the very first packet ever, and nothing after that!
	res = get_protocol_store()->udp->send_ipv4_udp(dhcp, dhcp_length, udp_source, 68, udp_dest, 67);
	if(res != 0) {
		get_vga_stream() << "Failed to send UDP response\n";
		return res;
	}

	return 0;
}
