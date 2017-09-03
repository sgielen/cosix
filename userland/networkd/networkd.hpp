#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cloudabi_types.h>

namespace networkd {
struct interface;
struct arp;
struct routing_table;
struct ip;

enum transport_proto {
	icmp = 0x01,
	tcp = 0x06,
	udp = 0x11,
};

}

std::string send_ifstore_command(std::string command);
std::vector<std::string> split_words(std::string str);
std::vector<std::shared_ptr<networkd::interface>> get_interfaces();
int get_raw_socket(std::string iface);
std::vector<std::string> get_addr_v4(std::string iface);
// ip is packed
void add_addr_v4(std::string iface, std::string ip, int cidr_prefix);
void start_dhclient(std::string iface);

/* reverse, pseudo */
std::pair<int, int> open_pseudo(cloudabi_filetype_t);

void dump_interfaces();
void dump_routing_table();
void dump_properties();

std::shared_ptr<networkd::interface> get_interface(std::string);
networkd::arp &get_arp();
networkd::routing_table &get_routing_table();
networkd::ip &get_ip();

std::string get_property(std::string name);
void set_property(std::string name, std::string value);
void unset_property(std::string name);
