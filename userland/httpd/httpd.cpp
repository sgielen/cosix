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
int port;

void program_main(const argdata_t *ad) {
	stdout = -1;
	tmpdir = -1;
	networkd = -1;
	port = 80;

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
		} else if(strcmp(keystr, "port") == 0) {
			argdata_get_int(value, &port);
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	// Listen socket: bound to 0.0.0.0:${port}, listening
	int listensock = cosix::networkd::get_socket(networkd, SOCK_STREAM, "", "0.0.0.0:" + std::to_string(port));

	while(1) {
		int accepted = accept(listensock, nullptr, nullptr);
		if(accepted < 0) {
			dprintf(stdout, "Failed to accept() TCP socket (%s)\n", strerror(errno));
			exit(1);
		}

		dprintf(stdout, "Incoming connection\n");
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
