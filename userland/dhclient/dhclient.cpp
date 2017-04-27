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
#include <vector>

int stdout;
int networkd;
std::string interface;

size_t send_if_command(std::string command, char *buf, size_t bufsize) {
	argdata_t *keys[] = {argdata_create_str_c("command"), argdata_create_str_c("interface")};
	argdata_t *values[] = {argdata_create_str_c(command.c_str()), argdata_create_str_c(interface.c_str())};
	argdata_t *req = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	size_t len;
	argdata_serialized_length(req, &len, 0);

	char rbuf[len];
	argdata_serialize(req, rbuf, 0);

	argdata_free(keys[0]);
	argdata_free(values[0]);
	argdata_free(req);

	write(networkd, rbuf, len);
	// TODO: set non-blocking flag once kernel supports it
	// this way, we can read until EOF instead of only 200 bytes
	ssize_t size = read(networkd, buf, bufsize);
	if(size <= 0) {
		perror("read");
		exit(1);
	}
	return size;
}

const argdata_t *ad_from_map(argdata_t *ad, std::string needle) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, needle.c_str()) == 0) {
			return value;
		}
	}
	return nullptr;
}

std::string string_from_ad(const argdata_t *ad) {
	const char *str;
	size_t len;
	argdata_get_str(ad, &str, &len);
	return std::string(str, len);
}

std::string string_from_map(argdata_t *ad, std::string needle) {
	const argdata_t *str = ad_from_map(ad, needle);
	if(str) {
		return string_from_ad(str);
	}
	return "";
}

std::string get_mac() {
	char buf[200];
	size_t size = send_if_command("mac", buf, sizeof(buf));
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return "";
	}
	return string_from_map(response, "mac");
}

std::string get_hwtype() {
	char buf[200];
	size_t size = send_if_command("hwtype", buf, sizeof(buf));
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return "";
	}
	return string_from_map(response, "hwtype");
}

std::vector<std::string> get_v4addr() {
	char buf[200];
	size_t size = send_if_command("addrv4", buf, sizeof(buf));
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	std::vector<std::string> res;
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return res;
	}
	const argdata_t *v4addrs = ad_from_map(response, "addrv4");
	argdata_seq_iterator_t it;
	argdata_seq_iterate(v4addrs, &it);
	const argdata_t *v4addr;
	while(argdata_seq_next(&it, &v4addr)) {
		res.push_back(string_from_ad(v4addr));
	}
	return res;
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

	dprintf(stdout, "MAC: %s\n", get_mac().c_str());
	dprintf(stdout, "HW type: %s\n", get_hwtype().c_str());
	dprintf(stdout, "Number of v4 addrs: %d\n", get_v4addr().size());

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
