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

int stdout;
int networkd;
std::string interface;

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
		} else if(strcmp(keystr, "networkd") == 0) {
			argdata_get_fd(value, &networkd);
		} else if(strcmp(keystr, "interface") == 0) {
			const char *ifstr;
			size_t iflen;
			argdata_get_str(value, &ifstr, &iflen);
			interface.assign(ifstr, iflen);
		}
	}

	dprintf(stdout, "dhclient started for interface %s\n", interface.c_str());
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	while(1) {
		// TODO: Check if lease is expiring
		//   If so, ask for an extension

		// TODO: Check if lease expired
		//   If so, drop IP

		// TODO: Check if the interface has no IP
		//   If so, ask for an IP

		// TODO: Sleep for min(60, lease-expiry-time)
		struct timespec ts = {.tv_sec = 60, .tv_nsec = 0};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
	}
}
