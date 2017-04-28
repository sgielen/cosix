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
	if(recvmsg(ifstore, &msg, 0) < 0) {
		perror("Failed to retrieve rawsock from ifstore");
		exit(1);
	}
	if(strncmp(buf, "OK", sizeof(buf)) != 0) {
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

std::vector<std::string> get_interfaces() {
	return split_words(send_ifstore_command("LIST"));
}

std::string get_hwtype(std::string iface) {
	return send_ifstore_command("HWTYPE " + iface);
}

std::string get_mac(std::string iface) {
	return send_ifstore_command("MAC " + iface);
}

std::map<std::string, std::vector<std::string>> ips_per_iface;

std::vector<std::string> get_addr_v4(std::string iface) {
	auto it = ips_per_iface.find(iface);
	if(it == ips_per_iface.end()) {
		return {};
	} else {
		return it->second;
	}
}

void add_addr_v4(std::string iface, std::string ip) {
	auto it = ips_per_iface.find(iface);
	if(it == ips_per_iface.end()) {
		ips_per_iface[iface] = {ip};
	} else {
		it->second.push_back(ip);
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
		std::string hwtype = get_hwtype(iface);
		std::string mac = get_mac(iface);

		dprintf(stdout, "* %s (type %s, MAC %s)\n", iface.c_str(), hwtype.c_str(), mac.c_str());
		for(auto &ip : get_addr_v4(iface)) {
			dprintf(stdout, "  IPv4: %s\n", ip.c_str());
		}
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
	for(auto &iface : get_interfaces()) {
		std::string hwtype = get_hwtype(iface);

		if(hwtype == "ETHERNET") {
			start_dhclient(iface);
		} else if(hwtype == "LOOPBACK") {
			add_addr_v4(iface, "127.0.0.1");
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
