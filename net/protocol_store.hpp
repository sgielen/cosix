#pragma once

namespace cloudos {

struct ethernet_implementation;
struct ip_implementation;
struct arp_implementation;
struct icmp_implementation;
struct udp_implementation;
struct tcp_implementation;
struct elfrun_implementation;
struct dhcp_client;

enum class protocol_t {
	ethernet,
	ip,
	arp,
	icmp,
	udp,
	tcp
};

struct protocol_store {
	protocol_store();

	ethernet_implementation *ethernet;
	ip_implementation *ip;
	arp_implementation *arp;
	icmp_implementation *icmp;
	udp_implementation *udp;
	tcp_implementation *tcp;
	elfrun_implementation *elfrun;

	// TODO: should make this per-interface and shouldn't store it here:
	dhcp_client *dhcp;
};

}
