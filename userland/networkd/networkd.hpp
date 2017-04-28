#include <string>
#include <vector>

std::string send_ifstore_command(std::string command);
std::vector<std::string> split_words(std::string str);
std::vector<std::string> get_interfaces();
std::string get_hwtype(std::string iface);
std::string get_mac(std::string iface);
int get_raw_socket(std::string iface);
std::vector<std::string> get_addr_v4(std::string iface);
void add_addr_v4(std::string iface, std::string ip);

void dump_interfaces();
