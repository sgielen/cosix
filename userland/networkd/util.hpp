#pragma once
#include <cloudabi_syscalls.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string>
#include <sstream>

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

inline std::pair<std::string, uint16_t> ipv4_port_pton(std::string ip_port) {
	size_t location = ip_port.find(':');
	if(location == std::string::npos) {
		throw std::runtime_error("ipv4_port_pton: no port");
	}
	std::string ip = ip_port.substr(0, location);
	std::string port = ip_port.substr(location + 1);
	std::stringstream ss;
	ss << port;
	uint16_t portnum;
	if(!(ss >> portnum)) {
		throw std::runtime_error("ipv4_port_pton: wrong port");
	}
	return std::make_pair(ipv4_pton(ip), portnum);
}

inline std::pair<std::string, uint8_t> ipv4_cidr_pton(std::string ip_cidr) {
	size_t location = ip_cidr.find('/');
	if(location == std::string::npos) {
		// assuming CIDR = 32
		return std::make_pair(ipv4_pton(ip_cidr), 32);
	}
	std::string ip = ip_cidr.substr(0, location);
	std::string cidr = ip_cidr.substr(location + 1);
	std::stringstream ss;
	ss << cidr;
	unsigned int cidr_prefix;
	if(!(ss >> cidr_prefix) || cidr_prefix > 32) {
		throw std::runtime_error("ipv4_cidr_pton: wrong cidr");
	}
	return std::make_pair(ipv4_pton(ip), cidr_prefix);
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
