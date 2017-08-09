#include <argdata.h>
#include <cloudabi_syscalls.h>
#include <fcntl.h>
#include <program.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capsicum.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <thread>
#include <vector>
#include <cassert>

#include <cosix/reverse.hpp>

int stdout;
int ifstore;

bool read_called = false;
bool isreadable_called = false;
bool isreadable = true;
bool finished = false;
const char *message = "foo";

struct pseudotest_handler : public cosix::reverse_handler {
	typedef cosix::pseudofd_t pseudofd_t;

	pseudotest_handler(int r) : reversefd(r) {}

	size_t pread(pseudofd_t pseudo, off_t, char *dest, size_t requested) override {
		if(pseudo != 0 || requested < 3) {
			dprintf(stdout, "Wrong pread() call\n");
			exit(1);
		}
		if(read_called) {
			dprintf(stdout, "Unexpected pread() call\n");
			exit(1);
		}
		read_called = true;
		isreadable = false;
		strncpy(dest, message, requested);
		return std::min(strlen(message), requested);
	}
	
	bool is_readable(pseudofd_t pseudo) override {
		if(pseudo != 0) {
			dprintf(stdout, "Wrong is_readable() call\n");
			exit(1);
		}
		isreadable_called = true;
		if(read_called && !thr.joinable()) {
			int rfd = reversefd;
			std::thread t([rfd, pseudo]() {
				std::this_thread::sleep_for(std::chrono::seconds(2));
				read_called = false;
				isreadable = true;
				message = "bar";
				cosix::pseudo_fd_becomes_readable(rfd, pseudo);
			});
			std::swap(thr, t);
		}
		return isreadable;
	}

private:
	int reversefd;
	std::thread thr;
};

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
		} else if(strcmp(keystr, "ifstore") == 0) {
			argdata_get_fd(value, &ifstore);
		}
		argdata_map_next(&it);
	}

	dprintf(stdout, "Pseudotest started!\n");
	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	auto pseudopair = cosix::open_pseudo(ifstore, CLOUDABI_FILETYPE_SOCKET_STREAM);
	auto reversefd = pseudopair.first;
	auto pseudofd = pseudopair.second;

	pseudotest_handler handler(reversefd);

	std::thread thr([&]() {
		while(!finished) try {
			auto res = handle_request(reversefd, &handler, 1000 * 1000 * 1000 /* 1 sec */);
			if(res != 0 && res != EAGAIN) {
				dprintf(stdout, "handle_request failed: %s\n", strerror(res));
				exit(1);
			}
		} catch(std::runtime_error &e) {
			dprintf(stdout, "handle_request failed because of exception: %s\n", e.what());
			exit(1);
		}
	});

	assert(!read_called);
	char buf[16];
	ssize_t s = read(pseudofd, buf, sizeof(buf));
	if(s < 0) {
		dprintf(stdout, "First read failed: %s\n", strerror(errno));
		exit(1);
	}
	if(s != 3 || strncmp(buf, "foo", 3) != 0) {
		dprintf(stdout, "Read wrong first buffer\n");
		exit(1);
	}
	assert(read_called);

	struct timeval tv;
	fd_set read_set;
	tv.tv_usec = 0;
	tv.tv_sec = 5;
	FD_ZERO(&read_set);
	FD_SET(pseudofd, &read_set);

	if(select(pseudofd + 1, &read_set, nullptr, nullptr, &tv) < 0) {
		dprintf(stdout, "select() failed\n");
		exit(1);
	}
	if(!FD_ISSET(pseudofd, &read_set)) {
		dprintf(stdout, "pseudofd is not ready for reading\n");
		exit(1);
	}
	
	assert(isreadable_called);
	assert(!read_called);

	s = read(pseudofd, buf, sizeof(buf));
	if(s < 0) {
		dprintf(stdout, "Second read failed: %s\n", strerror(errno));
		exit(1);
	}
	if(s != 3 || strncmp(buf, "bar", 3) != 0) {
		dprintf(stdout, "Read wrong second buffer\n");
		exit(1);
	}

	assert(read_called);

	finished = true;
	thr.join();
	dprintf(stdout, "Pseudo test finished successfully!\n");
	exit(0);

	// TODO: check that the select() blocked and the read() returned immediately
}
