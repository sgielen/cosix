#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/capsicum.h>
#include <cloudabi_syscalls.h>
#include <thread>
#include "client.hpp"
#include <vector>
#include <fcntl.h>
#include <map>
#include "interface.hpp"
#include "networkd.hpp"
#include "arp.hpp"
#include "util.hpp"
#include "routing_table.hpp"
#include "ip.hpp"
#include "udp_socket.hpp"

int stdout;
int bootfs;
int rootfs;
int ifstore;
std::mutex ifstore_mtx;

using namespace networkd;

std::string send_ifstore_command(std::string command) {
	std::lock_guard<std::mutex> lock(ifstore_mtx);

	write(ifstore, command.c_str(), command.length());
	char buf[200];
	ssize_t size = read(ifstore, buf, sizeof(buf));
	if(size < 0) {
		perror("Failed to read from ifstore");
		exit(1);
	}
	return std::string(buf, size);
}

int get_raw_socket(std::string iface) {
	std::lock_guard<std::mutex> lock(ifstore_mtx);

	std::string command = "RAWSOCK " + iface;
	write(ifstore, command.c_str(), command.length());

	char buf[20];
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(ifstore, &msg, 0);
	if(size < 0) {
		perror("Failed to retrieve rawsock from ifstore");
		exit(1);
	}
	if(strncmp(buf, "OK", size) != 0) {
		return -1;
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
		return -1;
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	return fdbuf[0];
}


std::vector<std::string> split_words(std::string str) {
	std::stringstream ss;
	ss << str;
	std::vector<std::string> res;
	while(ss >> str) {
		res.push_back(str);
	}
	return res;
}

std::vector<std::string> get_ifstore_interfaces() {
	return split_words(send_ifstore_command("LIST"));
}

std::string get_hwtype(std::string iface) {
	return send_ifstore_command("HWTYPE " + iface);
}

std::string get_mac(std::string iface) {
	return send_ifstore_command("MAC " + iface);
}

std::pair<int, int> open_pseudo() {
	std::lock_guard<std::mutex> lock(ifstore_mtx);
	write(ifstore, "PSEUDOPAIR", 10);
	char buf[20];
	buf[0] = 0;
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(2 * sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	if(recvmsg(ifstore, &msg, 0) < 0 || strncmp(buf, "OK", 2) != 0) {
		perror("Failed to retrieve pseudopair from ifstore");
		exit(1);
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(2 * sizeof(int))) {
		dprintf(stdout, "Pseudopair requested, but not given\n");
		exit(1);
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	return std::make_pair(fdbuf[0], fdbuf[1]);
}

using networkd::interface;

std::map<std::string, std::shared_ptr<interface>> interfaces;

std::vector<std::shared_ptr<interface>> get_interfaces() {
	std::vector<std::shared_ptr<interface>> res;
	for(auto &ifpair : interfaces) {
		res.push_back(ifpair.second);
	}
	return res;
}

std::shared_ptr<interface> get_interface(std::string name) {
	auto it = interfaces.find(name);
	if(it != interfaces.end()) {
		return it->second;
	} else {
		throw std::runtime_error("No such interface: " + name);
	}
}

std::vector<std::string> get_addr_v4(std::string iface) {
	auto it = interfaces.find(iface);
	if(it != interfaces.end()) {
		std::vector<std::string> res;
		for(auto &addr : it->second->get_ipv4addrs()) {
			res.push_back(ipv4_ntop(std::string(addr.ip, 4)));
		}
		return res;
	} else {
		return {};
	}
}

void add_addr_v4(std::string iface, std::string ip, int cidr_prefix) {
	auto interface = get_interface(iface);
	interface->add_ipv4addr(ip.c_str(), cidr_prefix);
	get_routing_table().add_link_route(interface, ip, cidr_prefix);
}

using networkd::arp;

arp a;
arp &get_arp() {
	return a;
}

using networkd::routing_table;

routing_table r;
routing_table &get_routing_table() {
	return r;
}

using networkd::ip;

ip i;
ip &get_ip() {
	return i;
}

std::mutex properties_mtx;
std::map<std::string, std::string> properties;

std::string get_property(std::string name) {
	std::lock_guard<std::mutex> lock(properties_mtx);
	auto it = properties.find(name);
	if(it == properties.end()) {
		throw std::runtime_error("No such property " + name);
	}
	return it->second;
}

void set_property(std::string name, std::string value) {
	std::lock_guard<std::mutex> lock(properties_mtx);
	properties[name] = value;
}

void unset_property(std::string name) {
	std::lock_guard<std::mutex> lock(properties_mtx);
	properties.erase(name);
}

void dump_properties() {
	std::lock_guard<std::mutex> lock(properties_mtx);
	for(auto &pair : properties) {
		dprintf(stdout, "* \"%s\" = \"%s\"\n", pair.first.c_str(), pair.second.c_str());
	}
}

void start_dhclient(std::string iface) {
	int bfd = openat(bootfs, "dhclient", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Failed to open dhclient: %s\n", strerror(errno));
		return;
	}

	int networkfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(networkfd < 0) {
		perror("networkfd");
		exit(1);
	}
	if(connectat(networkfd, rootfs, "networkd") < 0) {
		perror("connect");
		exit(1);
	}

	argdata_t *keys[] = {argdata_create_str_c("stdout"), argdata_create_str_c("networkd"), argdata_create_str_c("interface")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(networkfd), argdata_create_str_c(iface.c_str())};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "dhclient failed to spawn: %s\n", strerror(errno));
	} else {
		dprintf(stdout, "dhclient spawned for interface %s\n", iface.c_str());
	}

	close(bfd);
	close(networkfd);
}

void dump_interfaces() {
	for(auto &iface : get_interfaces()) {
		std::string name = iface->get_name();
		std::string hwtype = iface->get_hwtype();
		std::string mac = mac_ntop(iface->get_mac());

		dprintf(stdout, "* %s (type %s, MAC %s)\n", name.c_str(), hwtype.c_str(), mac.c_str());
		for(auto &ip : iface->get_ipv4addrs()) {
			std::string ip_show = ipv4_ntop(std::string(ip.ip, 4));
			dprintf(stdout, "  IPv4: %s\n", ip_show.c_str());
		}
	}
}

void dump_routing_table() {
	auto table = get_routing_table().copy_table();
	dprintf(stdout, "IP Subnet          Gateway         Device\n");
	for(auto &entry : table) {
		auto iface = entry.iface.lock();
		if(!iface) {
			continue;
		}
		std::string dev = iface->get_name();
		std::string ipd;
		if(entry.ip.empty()) {
			ipd = "default";
		} else {
			ipd = ipv4_ntop(entry.ip) + "/" + std::to_string(entry.cidr_prefix);
		}
		int spaces = 19 - ipd.size();
		std::string space(spaces, ' ');
		dprintf(stdout, "%s%s", ipd.c_str(), space.c_str());
		ipd = ipv4_ntop(entry.gateway_ip);
		spaces = 16 - ipd.size();
		space.resize(spaces, ' ');
		dprintf(stdout, "%s%s%s\n", ipd.c_str(), space.c_str(), dev.c_str());
	}
}

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "bootfs") == 0) {
			argdata_get_fd(value, &bootfs);
		} else if(strcmp(keystr, "rootfs") == 0) {
			argdata_get_fd(value, &rootfs);
		} else if(strcmp(keystr, "ifstore") == 0) {
			argdata_get_fd(value, &ifstore);
		}
	}

	dprintf(stdout, "Networkd started!\n");
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(listenfd < 0) {
		perror("socket");
		exit(1);
	}
	if(bindat(listenfd, rootfs, "networkd") < 0) {
		perror("bindat");
		exit(1);
	}
	if(listen(listenfd, SOMAXCONN) < 0) {
		perror("listen");
		exit(1);
	}

	dump_interfaces();
	for(auto &iface : get_ifstore_interfaces()) {
		std::string mac = get_mac(iface);
		std::string hwtype = get_hwtype(iface);
		int rawsock = get_raw_socket(iface);

		if(rawsock < 0) {
			dprintf(stdout, "Failed to get a raw socket to interface %s, skipping\n", iface.c_str());
			continue;
		}

		auto interface = std::make_shared<networkd::interface>(iface, mac_pton(mac), hwtype, rawsock);
		interfaces[iface] = interface;
		interface->start();

		if(hwtype == "ETHERNET") {
			start_dhclient(iface);
		} else if(hwtype == "LOOPBACK") {
			add_addr_v4(iface, ipv4_pton("127.0.0.1"), 8);
		}
	}

	while(1) {
		int client = accept(listenfd, NULL, NULL);
		if(client < 0) {
			perror("accept");
			exit(1);
		}
		std::thread clientthread([client](){
			networkd::client c(stdout, client);
			c.run();
		});
		clientthread.detach();
	}
}
