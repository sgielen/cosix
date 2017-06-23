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

	// First test, to see IP stack sending behaviour in PCAP dumps:
	try {
		int sock0 = networkd_get_socket(SOCK_STREAM, "138.201.24.102:8765", "");
		write(sock0, "Hello World!\n", 13);
	} catch(std::runtime_error &e) {
		dprintf(stdout, "Failed to run TCP test: %s\n", e.what());
	}

	bool running = true;
	std::thread server([&]() {
		// Listen socket: bound to 0.0.0.0:1234, listening
		int listensock = networkd_get_socket(SOCK_STREAM, "", "0.0.0.0:1234");

		// Accept a socket, read data, rot13 it, send it back
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
		if(address.sin_family != AF_INET || strcmp(ip, "127.0.0.1") != 0 /* port is random */)
		{
			dprintf(stdout, "Address on socket is incorrect (%s:%hu)\n", ip, address.sin_port);
			exit(1);
		}
		char buf[16];
		while(running) {
			ssize_t res = read(accepted, buf, sizeof(buf));
			if(res < 0) {
				dprintf(stdout, "Failed to receive data over TCP (%s)\n", strerror(errno));
				exit(1);
			}
			for(ssize_t i = 0; i < res; ++i) {
				if(buf[i] >= 'A' && buf[i] <= 'Z') {
					buf[i] = 'A' + (((buf[i] - 'A') + 13) % 26);
				} else if(buf[i] >= 'a' && buf[i] <= 'z') {
					buf[i] = 'a' + (((buf[i] - 'a') + 13) % 26);
				}
			}
			if(write(accepted, buf, res) != res) {
				dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
				exit(1);
			}
		}
	});

	// Wait a second for listening thread to come up
	struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);

	// Socket: connected to 127.0.0.1:1234, not bound
	int connected = networkd_get_socket(SOCK_STREAM, "127.0.0.1:1234", "");
	if(write(connected, "Foo bar!", 8) != 8) {
		dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	char buf[32];
	if(read(connected, buf, sizeof(buf)) != 8) {
		dprintf(stdout, "Failed to receive data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(memcmp(buf, "Sbb one!", 8) != 0) {
		buf[8] = 0;
		dprintf(stdout, "ROT13 data received is incorrect: \"%s\"\n", buf);
		exit(1);
	}

	if(write(connected, "Mumblebumble", 12) != 12) {
		dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(write(connected, "Blamblam", 8) != 8) {
		dprintf(stdout, "Failed to write additional data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	// wait for a second for the response to arrive
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
	if(read(connected, buf, sizeof(buf)) != 20) {
		dprintf(stdout, "Failed to receive all data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(memcmp(buf, "ZhzoyrohzoyrOynzoynz", 20) != 0) {
		buf[20] = 0;
		dprintf(stdout, "ROT13 data received is incorrect: \"%s\"\n", buf);
		exit(1);
	}
	
	// TODO: send packets from invalid TCP stacks, see if the kernel copes
	// TODO: send packets over very bad connections (VDE switch?), see if communication
	// still works well

	running = false;
	// bump the server thread if it's blocked on read()
	write(connected, "bump", 4);
	server.join();

	dprintf(stdout, "All TCP traffic seems correct!\n");
	exit(0);
}
