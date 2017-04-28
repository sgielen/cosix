#include "client.hpp"
#include "networkd.hpp"
#include <argdata.h>
#include <unistd.h>
#include <sys/socket.h>

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
		argdata_t *message = argdata_from_buffer(buf, size);

		std::string command;
		std::string iface;
		std::string arg;

		argdata_map_iterator_t it;
		const argdata_t *key;
		const argdata_t *value;
		argdata_map_iterate(message, &it);
		while (argdata_map_next(&it, &key, &value)) {
			const char *keystr;
			if(argdata_get_str_c(key, &keystr) != 0) {
				continue;
			}

			const char *cstr;
			size_t len;
			if(strcmp(keystr, "command") == 0) {
				argdata_get_str(value, &cstr, &len);
				command.assign(cstr, len);
			} else if(strcmp(keystr, "interface") == 0) {
				argdata_get_str(value, &cstr, &len);
				iface.assign(cstr, len);
			} else if(strcmp(keystr, "arg") == 0) {
				argdata_get_str(value, &cstr, &len);
				arg.assign(cstr, len);
			}
		}

		if(command.empty()) {
			if(!send_error("Command is mandatory")) {
				return;
			}
		}
		if(iface.empty()) {
			if(!send_error("Interface is mandatory")) {
				return;
			}
		}

		if(command == "dump") {
			dump_interfaces();
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
			std::string mac = get_mac(iface);

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
			std::string hwtype = get_hwtype(iface);

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
			auto ipv4 = get_addr_v4(iface);
			argdata_t *addresses[ipv4.size()];
			for(size_t i = 0; i < ipv4.size(); ++i) {
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
			add_addr_v4(iface, arg);
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
		} else if(command == "rawsock") {
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
		} else {
			if(!send_error("No such command")) {
				return;
			}
		}
	}
}
