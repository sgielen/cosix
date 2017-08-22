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
#include <dirent.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/procdesc.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <mstd/range.hpp>

#include <cosix/networkd.hpp>
#include <cosix/reverse.hpp>

#include "tterm.hpp"

int stdout;
int tmpdir;
int initrd;
int networkd;
int procfs;
int bootfs;
int ifstore;

int port;
int shell;

using namespace telnetd;

void program_main(const argdata_t *ad) {
	stdout = -1;
	networkd = -1;
	procfs = -1;
	bootfs = -1;
	tmpdir = -1;
	initrd = -1;
	port = 22;
	shell = -1;
	ifstore = -1;

	{
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
			} else if(strcmp(keystr, "procfs") == 0) {
				argdata_get_fd(value, &procfs);
			} else if(strcmp(keystr, "bootfs") == 0) {
				argdata_get_fd(value, &bootfs);
			} else if(strcmp(keystr, "tmpdir") == 0) {
				argdata_get_fd(value, &tmpdir);
			} else if(strcmp(keystr, "initrd") == 0) {
				argdata_get_fd(value, &initrd);
			} else if(strcmp(keystr, "port") == 0) {
				argdata_get_int(value, &port);
			} else if(strcmp(keystr, "shell") == 0) {
				argdata_get_fd(value, &shell);
			} else if(strcmp(keystr, "ifstore") == 0) {
				argdata_get_fd(value, &ifstore);
			}
			argdata_map_next(&it);
		}
	}

	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	// Listen socket: bound to 0.0.0.0:${port}, listening
	int listensock = cosix::networkd::get_socket(networkd, SOCK_STREAM, "", "0.0.0.0:" + std::to_string(port));

	while(1) {
		// Perform a select() so we don't have an accept() call outstanding on the reverse FD
		// blocking other requests. TODO: remove this once it's not necessary anymore.
		fd_set rdset;
		FD_ZERO(&rdset);
		FD_SET(listensock, &rdset);
		select(listensock + 1, &rdset, nullptr, nullptr, nullptr);

		int client = accept(listensock, nullptr, nullptr);
		if(client < 0) {
			fprintf(stderr, "Failed to accept() TCP socket: %s\n", strerror(errno));
			exit(1);
		}
		const char banner[] = "Welcome to Cosix -- starting shell...\n";
		if(write(client, banner, sizeof(banner)) < 0) {
			fprintf(stderr, "Failed to write() banner: %s\n", strerror(errno));
			close(client);
			continue;
		}

		auto revpseu = cosix::open_pseudo(ifstore, CLOUDABI_FILETYPE_CHARACTER_DEVICE);

		// start terminal emulator
		std::thread tthr([client, revpseu](){
			telnet_terminal::run(client, revpseu.first);
			close(client);
			close(revpseu.first);
			close(revpseu.second);
		});
		tthr.detach();

		std::unique_ptr<argdata_t> keys[] =
			{argdata_t::create_str("terminal"), argdata_t::create_str("tmpdir"),
			argdata_t::create_str("initrd"), argdata_t::create_str("networkd"),
			argdata_t::create_str("procfs"), argdata_t::create_str("bootfs")};
		std::unique_ptr<argdata_t> values[] =
			{argdata_t::create_fd(revpseu.second), argdata_t::create_fd(tmpdir),
			argdata_t::create_fd(initrd), argdata_t::create_fd(networkd),
			argdata_t::create_fd(procfs), argdata_t::create_fd(bootfs)};
		std::vector<argdata_t*> key_ptrs;
		std::vector<argdata_t*> value_ptrs;

		for(auto &kkey : mstd::range<std::unique_ptr<argdata_t>>(keys)) {
			key_ptrs.push_back(kkey.get());
		}
		for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(values)) {
			value_ptrs.push_back(value.get());
		}
		auto client_ad = argdata_t::create_map(key_ptrs, value_ptrs);

		int client_pfd = program_spawn(shell, client_ad.get());
		if(client_pfd < 0) {
			fprintf(stderr, "Failed to spawn shell: %s\n", strerror(errno));
			exit(1);
		}
		std::thread thr([client_pfd]() {
			siginfo_t si;
			pdwait(client_pfd, &si, 0);
			close(client_pfd);
		});
		thr.detach();
	}
}
