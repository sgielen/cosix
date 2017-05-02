#pragma once
#include <cloudabi_syscalls.h>
#include <arpa/inet.h>
#include <sys/socket.h>

inline cloudabi_timestamp_t monotime() {
	cloudabi_timestamp_t ts = 0;
	cloudabi_sys_clock_time_get(CLOUDABI_CLOCK_MONOTONIC, 0, &ts);
	return ts;
}

inline std::string ipv4_ntop(std::string ip) {
	char out[INET_ADDRSTRLEN];
	return inet_ntop(AF_INET, ip.c_str(), out, sizeof(out));
}

inline std::string ipv4_pton(std::string ip) {
	char out[4];
	if(inet_pton(AF_INET, ip.c_str(), out) == 1) {
		return std::string(out, 4);
	} else {
		throw std::runtime_error("ipv4_pton failed");
	}
}

inline std::string mac_ntop(std::string mac) {
	std::string res;
	res.reserve(17);
	for(auto &byte : mac) {
		if(!res.empty()) {
			res += ':';
		}
		char num[4];
		snprintf(num, sizeof(num), "%02x", byte);
		res += num;
	}
	if(res.empty()) {
		res = "00:00:00:00:00:00";
	}
	return res;
}

inline std::string mac_pton(std::string mac) {
	std::string res;
	res.reserve(6);
	size_t offset = 0;
	// while we can read 2 or 3 bytes ahead
	while(offset + 2 <= mac.length()) {
		std::string word = mac.substr(offset, 2);
		offset += 2;

		unsigned int w;
		sscanf(word.c_str(), "%02x", &w);
		res.push_back(static_cast<char>(w));

		// skip optional delimiter
		if(offset < mac.length() && (mac[offset] == ':' || mac[offset] == '-')) {
			offset++;
		}
	}
	return res;
}
