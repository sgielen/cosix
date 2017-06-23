#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <argdata.hpp>
#include <cloudabi_syscalls.h>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/procdesc.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int stdout;
int tmpdir;
int networkd;

int networkd_get_socket(int type, std::string connect, std::string bind) {
	std::string command;
	if(type == SOCK_DGRAM) {
		command = "udpsock";
	} else if(type == SOCK_STREAM) {
		command = "tcpsock";
	} else {
		throw std::runtime_error("Unknown type of socket to get");
	}

	std::unique_ptr<argdata_t> keys[] =
		{argdata_t::create_str("command"), argdata_t::create_str("connect"), argdata_t::create_str("bind")};
	std::unique_ptr<argdata_t> values[] =
		{argdata_t::create_str(command.c_str()), argdata_t::create_str(connect.c_str()), argdata_t::create_str(bind.c_str())};
	std::vector<argdata_t*> key_ptrs;
	std::vector<argdata_t*> value_ptrs;
	
	for(auto &key : mstd::range<std::unique_ptr<argdata_t>>(keys)) {
		key_ptrs.push_back(key.get());
	}
	for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(values)) {
		value_ptrs.push_back(value.get());
	}
	auto map = argdata_t::create_map(key_ptrs, value_ptrs);

	std::vector<unsigned char> rbuf;
	map->serialize(rbuf);

	write(networkd, rbuf.data(), rbuf.size());
	// TODO: set non-blocking flag once kernel supports it
	// this way, we can read until EOF instead of only 200 bytes
	// TODO: for a generic implementation, MSG_PEEK to find the number
	// of file descriptors
	uint8_t buf[1500];
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(networkd, &msg, 0);
	if(size < 0) {
		perror("read");
		exit(1);
	}
	auto response = argdata_t::create_from_buffer(mstd::range<unsigned char const>(&buf[0], size));
	int fdnum = -1;
	for(auto i : response->as_map()) {
		auto key = i.first->as_str();
		if(key == "error") {
			throw std::runtime_error("Failed to retrieve TCP socket from networkd: " + i.second->as_str().to_string());
		} else if(key == "fd") {
			fdnum = *i.second->get_fd();
		}
	}
	if(fdnum != 0) {
		throw std::runtime_error("Ifstore TCP socket not received");
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
		dprintf(stdout, "Ifstore socket requested, but not given\n");
		exit(1);
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	return fdbuf[0];
}

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_get(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			argdata_map_next(&it);
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "networkd") == 0) {
			argdata_get_fd(value, &networkd);
		} else if(strcmp(keystr, "tmpdir") == 0) {
			argdata_get_fd(value, &tmpdir);
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	// Listen socket: bound to 0.0.0.0:80, listening
	int listensock = networkd_get_socket(SOCK_STREAM, "", "0.0.0.0:80");

	while(1) {
		sockaddr_in address;
		sockaddr *address_ptr = reinterpret_cast<sockaddr*>(&address);
		size_t address_size = sizeof(address);
		int accepted = accept(listensock, address_ptr, &address_size);
		if(accepted < 0) {
			dprintf(stdout, "Failed to accept() TCP socket (%s)\n", strerror(errno));
			exit(1);
		}

		char ip[16];
		inet_ntop(address.sin_family, &address.sin_addr, ip, sizeof(ip));
		dprintf(stdout, "Incoming connection from %s:%hu\n", ip, address.sin_port);
		char buf[512];
		buf[0] = 0;
		strlcat(buf, "HTTP/1.1 200 OK\r\n", sizeof(buf));
		strlcat(buf, "Server: cosix/0.0\r\n", sizeof(buf));
		strlcat(buf, "Content-Type: text/html; charset=UTF-8\r\n", sizeof(buf));
		strlcat(buf, "Transfer-Encoding: chunked\r\n", sizeof(buf));
		strlcat(buf, "Connection: close\r\n\r\n", sizeof(buf));
		strlcat(buf, "40\r\n", sizeof(buf));
		strlcat(buf, "<!DOCTYPE html><html><body><h1>Hello world!</h1></body></html>\r\n\r\n0\r\n\r\n", sizeof(buf));
		write(accepted, buf, strlen(buf));
		// TODO: wait until all data is sent, then close accepted
	}

	exit(0);
}
