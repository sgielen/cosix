#pragma once
#include <string>
#include <vector>
#include <memory>

namespace networkd {
struct interface;
struct arp;
}

std::string send_ifstore_command(std::string command);
std::vector<std::string> split_words(std::string str);
std::vector<std::shared_ptr<networkd::interface>> get_interfaces();
int get_raw_socket(std::string iface);
std::vector<std::string> get_addr_v4(std::string iface);
void add_addr_v4(std::string iface, std::string ip);

void dump_interfaces();

std::shared_ptr<networkd::interface> get_interface(std::string);
networkd::arp &get_arp();
