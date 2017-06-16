#include "client.hpp"
#include "networkd.hpp"
#include <argdata.h>
#include <unistd.h>
#include <sys/socket.h>
#include "interface.hpp"
#include "util.hpp"
#include "udp_socket.hpp"
#include "tcp_socket.hpp"
#include "ip.hpp"
#include "routing_table.hpp"

using namespace networkd;

client::client(int l, int f)
: logfd(l)
, fd(f)
{
}

client::~client()
{
	close(fd);
}

bool client::send_response(argdata_t *response) {
	size_t reslen;
	size_t fdlen;
	argdata_serialized_length(response, &reslen, &fdlen);

	struct iovec iov = {
		.iov_base = malloc(reslen),
		.iov_len = reslen,
	};
	int *fds = reinterpret_cast<int*>(malloc(fdlen * sizeof(int)));
	fdlen = argdata_serialize(response, iov.iov_base, fds);

	alignas(struct cmsghdr) char control[CMSG_SPACE(fdlen * sizeof(int))];
	struct msghdr message = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof(control),
		.msg_flags = 0,
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
	cmsg->cmsg_len = CMSG_LEN(fdlen * sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	for(size_t i = 0; i < fdlen; ++i) {
		fdbuf[i] = fds[i];
	}
	if(sendmsg(fd, &message, 0) != static_cast<ssize_t>(reslen)) {
		perror("sendmsg");
		free(iov.iov_base);
		free(fds);
		return false;
	}

	free(iov.iov_base);
	free(fds);
	return true;
}

bool client::send_error(std::string error) {
	argdata_t *keys[] = {argdata_create_str_c("error")};
	argdata_t *values[] = {argdata_create_str_c(error.c_str())};
	argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
	bool success = send_response(response);
	argdata_free(keys[0]);
	argdata_free(values[0]);
	argdata_free(response);
	return success;
}

void client::run() {
	while(1) {
		char buf[200];
		// TODO: set non-blocking flag once kernel supports it
		// this way, we can read until EOF instead of only 200 bytes
		ssize_t size = read(fd, buf, sizeof(buf));
		if(size < 0) {
			perror("read");
			return;
		}
		if(size == 0) {
			continue;
		}
		argdata_t *message = argdata_from_buffer(buf, size, nullptr, nullptr);

		std::string command;
		std::string iface;
		std::string arg;
		std::string value;
		std::string bind;
		std::string connect;

		argdata_map_iterator_t it;
		const argdata_t *arg_key;
		const argdata_t *arg_value;
		argdata_map_iterate(message, &it);
		while (argdata_map_next(&it, &arg_key, &arg_value)) {
			const char *keystr;
			if(argdata_get_str_c(arg_key, &keystr) != 0) {
				continue;
			}

			const char *cstr;
			size_t len;
			if(argdata_get_str(arg_value, &cstr, &len) != 0) {
				continue;
			}
			if(strcmp(keystr, "command") == 0) {
				command.assign(cstr, len);
			} else if(strcmp(keystr, "interface") == 0) {
				iface.assign(cstr, len);
			} else if(strcmp(keystr, "arg") == 0) {
				arg.assign(cstr, len);
			} else if(strcmp(keystr, "value") == 0) {
				value.assign(cstr, len);
			} else if(strcmp(keystr, "bind") == 0) {
				bind.assign(cstr, len);
			} else if(strcmp(keystr, "connect") == 0) {
				connect.assign(cstr, len);
			}
		}

		if(command.empty()) {
			if(!send_error("Command is mandatory")) {
				return;
			}
			continue;
		}

		if(command == "dump") {
			dump_interfaces();
			dump_routing_table();
			dump_properties();
			argdata_t *keys[] = {argdata_create_str_c("dumped")};
			argdata_t *values[] = {argdata_create_str_c("ok")};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "mac") {
			if(iface.empty()) {
				if(!send_error("Interface is mandatory")) {
					return;
				}
				continue;
			}
			std::string mac;
			try {
				auto interface = get_interface(iface);
				mac = mac_ntop(interface->get_mac());
			} catch(std::runtime_error&) {
				mac = "ERROR";
			}

			argdata_t *keys[] = {argdata_create_str_c("mac")};
			argdata_t *values[] = {argdata_create_str_c(mac.c_str())};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "hwtype") {
			if(iface.empty()) {
				if(!send_error("Interface is mandatory")) {
					return;
				}
				continue;
			}
			std::string hwtype;
			try {
				auto interface = get_interface(iface);
				hwtype = interface->get_hwtype();
			} catch(std::runtime_error&) {
				hwtype = "ERROR";
			}

			argdata_t *keys[] = {argdata_create_str_c("hwtype")};
			argdata_t *values[] = {argdata_create_str_c(hwtype.c_str())};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "addrv4") {
			if(iface.empty()) {
				if(!send_error("Interface is mandatory")) {
					return;
				}
				continue;
			}
			auto ipv4 = get_addr_v4(iface);
			argdata_t *addresses[ipv4.size()];
			for(size_t i = 0; i < ipv4.size(); ++i) {
				ipv4[i] = ipv4_ntop(ipv4[i]);
				addresses[i] = argdata_create_str_c(ipv4[i].c_str());
			}

			argdata_t *keys[] = {argdata_create_str_c("addrv4")};
			argdata_t *values[] = {argdata_create_seq(addresses, ipv4.size())};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			for(size_t i = 0; i < ipv4.size(); ++i) {
				argdata_free(addresses[i]);
			}
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "add_addrv4") {
			if(iface.empty()) {
				if(!send_error("Interface is mandatory")) {
					return;
				}
				continue;
			}
			auto ip_cidr = ipv4_cidr_pton(arg);
			add_addr_v4(iface, ip_cidr.first, ip_cidr.second);
			argdata_t *keys[] = {argdata_create_str_c("added")};
			argdata_t *values[] = {argdata_create_str_c("ok")};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "get-property") {
			if(arg.empty()) {
				if(!send_error("arg is mandatory")) {
					return;
				}
				continue;
			}
			// value may give a default to return if the property doesn't exist
			try {
				value = get_property(arg);
			} catch(std::runtime_error &) {
				if(!send_error("property is unset")) {
					return;
				}
				continue;
			}
			argdata_t *keys[] = {argdata_create_str_c("property"), argdata_create_str_c("value")};
			argdata_t *values[] = {argdata_create_str_c(arg.c_str()), argdata_create_str_c(value.c_str())};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(keys[1]);
			argdata_free(values[0]);
			argdata_free(values[1]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "set-property") {
			if(arg.empty()) {
				if(!send_error("arg is mandatory")) {
					return;
				}
				continue;
			}

			// on specific args, immediately take action
			if(arg == "defaultgateway") {
				if(iface.empty()) {
					if(!send_error("interface is mandatory for setting defaultgateway")) {
						return;
					}
					continue;
				}
				auto interface = get_interface(iface);
				if(!interface) {
					if(!send_error("interface with that name doesn't exist")) {
						return;
					}
					continue;
				}
				std::string gateway = ipv4_pton(value);
				get_routing_table().set_default_gateway(interface, gateway);
			}

			set_property(arg, value);
			argdata_t *keys[] = {argdata_create_str_c("set")};
			argdata_t *values[] = {argdata_create_str_c("ok")};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "unset-property") {
			if(arg.empty()) {
				if(!send_error("arg is mandatory")) {
					return;
				}
				continue;
			}

			// on specific args, immediately take action
			if(arg == "defaultgateway") {
				if(iface.empty()) {
					if(!send_error("interface is mandatory for unsetting defaultgateway")) {
						return;
					}
					continue;
				}
				auto interface = get_interface(iface);
				if(!interface) {
					if(!send_error("interface with that name doesn't exist")) {
						return;
					}
					continue;
				}
				get_routing_table().unset_default_gateway(interface);
			}

			unset_property(arg);
			argdata_t *keys[] = {argdata_create_str_c("unset")};
			argdata_t *values[] = {argdata_create_str_c("ok")};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			if(!success) {
				return;
			}
		} else if(command == "rawsock") {
			if(iface.empty()) {
				if(!send_error("Interface is mandatory")) {
					return;
				}
				continue;
			}
			int raw = get_raw_socket(iface);
			if(raw < 0) {
				if(!send_error("Couldn't obtain raw socket")) {
					return;
				}
				continue;
			}
			argdata_t *keys[] = {argdata_create_str_c("fd")};
			argdata_t *values[] = {argdata_create_fd(raw)};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			close(raw);
			if(!success) {
				return;
			}
		} else if(command == "udpsock" || command == "tcpsock") {
			if(bind.empty() && connect.empty()) {
				if(!send_error("Need bind and/or connect parameters to create udp/tcp socket")) {
					return;
				}
				continue;
			}

			std::string local_ip(4, 0);
			uint16_t local_port = 0;
			std::string peer_ip;
			uint16_t peer_port = 0;
			cloudabi_filetype_t filetype = command == "udpsock" ? CLOUDABI_FILETYPE_SOCKET_DGRAM : CLOUDABI_FILETYPE_SOCKET_STREAM;

			if(!bind.empty()) {
				auto bind_ip_port = ipv4_port_pton(bind);
				local_ip = bind_ip_port.first;
				local_port = bind_ip_port.second;
			}
			if(!connect.empty()) {
				auto connect_ip_port = ipv4_port_pton(connect);
				peer_ip = connect_ip_port.first;
				peer_port = connect_ip_port.second;
			}

			if(local_port == 0) {
				// TODO: ensure port is unused
				// IANA ephemeral ports range
				local_port = 49152 + rand() % (65535 - 49152);
			}
			if(!peer_ip.empty() && (local_ip.empty() || local_ip == std::string(4, 0))) {
				// Find what interface we can use to reach the destination
				auto rule = get_routing_table().routing_rule_for_ip(peer_ip);
				if(!rule) {
					if(!send_error("Failed to auto-bind")) {
						return;
					}
					continue;
				}
				auto bind_iface = rule->second;
				auto l_ip = bind_iface->get_primary_ipv4addr();
				if(!l_ip) {
					if(!send_error("Failed to auto-bind")) {
						return;
					}
					continue;
				}
				local_ip = *l_ip;
			}

			auto rev_pseu = open_pseudo(filetype);
			if(rev_pseu.first <= 0 || rev_pseu.second <= 0) {
				if(!send_error("Failed to open pseudo pair")) {
					return;
				}
				continue;
			}

			std::shared_ptr<ip_socket> socket;
			if(command == "udpsock") {
				socket = std::make_shared<udp_socket>(local_ip, local_port, peer_ip, peer_port, 0, rev_pseu.first);
			} else {
				socket = std::make_shared<tcp_socket>(local_ip, local_port, peer_ip, peer_port, 0, rev_pseu.first);
			}
			// TODO: return EADDRINUSE if this socket is bound uniquely and something is already
			// bound to this IP:port combination (or 0.0.0.0:port)
			auto res = get_ip().register_socket(socket);

			if(res != 0) {
				if(!send_error("Failed to register socket")) {
					return;
				}
				continue;
			}

			res = socket->establish();
			if(res != 0) {
				if(!send_error("Failed to establish connection")) {
					return;
				}
				continue;
			}

			socket->start();

			argdata_t *keys[] = {argdata_create_str_c("fd")};
			argdata_t *values[] = {argdata_create_fd(rev_pseu.second)};
			argdata_t *response = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));
			bool success = send_response(response);
			argdata_free(keys[0]);
			argdata_free(values[0]);
			argdata_free(response);
			close(rev_pseu.second);
			if(!success) {
				return;
			}
		} else {
			if(!send_error("No such command")) {
				return;
			}
		}
	}
}
