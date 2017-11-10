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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
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
	// Connect to 8.8.8.8:1234, send packet.
	int sock0 = cosix::networkd::get_socket(networkd, SOCK_DGRAM, "8.8.8.8:1234", "");
	write(sock0, "Hello Google!", 13);

	// Socket A: bound to 0.0.0.0:1234, connected to 127.0.0.1:5678
	// Send packet over A: nothing happens
	int sockA = cosix::networkd::get_socket(networkd, SOCK_DGRAM, "127.0.0.1:5678", "0.0.0.0:1234");
	write(sockA, "Hello world!", 12);

	// Socket B: bound to 127.0.0.1:5678, connected to 127.0.0.1:1234
	// Send packet over B, read from A
	// Send packet over A, read from B
	int sockB = cosix::networkd::get_socket(networkd, SOCK_DGRAM, "127.0.0.1:1234", "127.0.0.1:5678");
	if(write(sockB, "Foo bar!", 8) != 8) {
		dprintf(stdout, "Failed to write data over UDP (%s)\n", strerror(errno));
		exit(1);
	}
	char buf[16];
	if(read(sockA, buf, sizeof(buf)) != 8 || memcmp(buf, "Foo bar!", 8) != 0) {
		dprintf(stdout, "Failed to receive data over UDP (%s)\n", strerror(errno));
		exit(1);
	}

	dprintf(stdout, "All UDP traffic seems correct!\n");
	exit(0);
}
