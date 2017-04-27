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

int stdout;
int rootfs;
int ifstore;

using namespace networkd;

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
		} else if(strcmp(keystr, "rootfs") == 0) {
			argdata_get_fd(value, &rootfs);
		} else if(strcmp(keystr, "ifstore") == 0) {
			argdata_get_fd(value, &ifstore);
		}
	}

	dprintf(stdout, "Networkd started!\n");
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	// TODO: enumerate interfaces
	// TODO: if an interface is type ethernet, start a DHCP client on it
	// TODO: routing table
	// TODO: offer an interface to get UDP or TCP sockets via rootfs

	write(ifstore, "LIST", 4);
	char buf[200];
	size_t size = read(ifstore, buf, sizeof(buf));
	buf[size] = 0;

	// TODO: move this to a networkd
	std::stringstream ss;
	ss << buf;
	std::string iface;
	dprintf(stdout, "Interfaces:\n");
	while(ss >> iface) {
		std::string cmd = "HWTYPE " + iface;
		write(ifstore, cmd.c_str(), cmd.length());
		size = read(ifstore, buf, sizeof(buf));
		buf[size] = 0;
		std::string type = buf;

		cmd = "MAC " + iface;
		write(ifstore, cmd.c_str(), cmd.length());
		size = read(ifstore, buf, sizeof(buf));
		buf[size] = 0;
		std::string mac = buf;

		dprintf(stdout, "* %s (type %s, MAC %s)\n", iface.c_str(), type.c_str(), mac.c_str());

		cmd = "ADDRV4 " + iface;
		write(ifstore, cmd.c_str(), cmd.length());
		size = read(ifstore, buf, sizeof(buf));
		buf[size] = 0;
		std::stringstream ips;
		ips << buf;
		std::string ip;
		while(ips >> ip) {
			dprintf(stdout, "  IPv4: %s\n", ip.c_str());
		}
	}

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
