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

#include <cosix/networkd.hpp>

int stdout;
int tmpdir;
int networkd;

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
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	// First test, to see IP stack sending behaviour in PCAP dumps:
	try {
		int sock0 = cosix::networkd::get_socket(networkd, SOCK_STREAM, "138.201.24.102:8765", "");
		write(sock0, "Hello World!\n", 13);
	} catch(std::runtime_error &e) {
		dprintf(stdout, "Failed to run TCP test: %s\n", e.what());
	}

	bool running = true;
	std::thread server([&]() {
		// Listen socket: bound to 0.0.0.0:1234, listening
		int listensock = cosix::networkd::get_socket(networkd, SOCK_STREAM, "", "0.0.0.0:1234");

		// Accept a socket, read data, rot13 it, send it back
		int accepted = accept(listensock, nullptr, nullptr);
		if(accepted < 0) {
			dprintf(stdout, "Failed to accept() TCP socket (%s)\n", strerror(errno));
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
	int connected = cosix::networkd::get_socket(networkd, SOCK_STREAM, "127.0.0.1:1234", "");
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
