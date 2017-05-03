#pragma once
#include <string>
#include <vector>
#include <memory>

namespace networkd {
struct interface;
struct arp;
struct routing_table;
}

std::string send_ifstore_command(std::string command);
std::vector<std::string> split_words(std::string str);
std::vector<std::shared_ptr<networkd::interface>> get_interfaces();
int get_raw_socket(std::string iface);
std::vector<std::string> get_addr_v4(std::string iface);
void add_addr_v4(std::string iface, std::string ip, int cidr_prefix, std::string gateway_ip = std::string());

void dump_interfaces();
void dump_routing_table();

std::shared_ptr<networkd::interface> get_interface(std::string);
networkd::arp &get_arp();
networkd::routing_table &get_routing_table();
