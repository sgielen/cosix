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
#include <cassert>
#include <cosix/reverse.hpp>
#include "tmpfs.hpp"

int device = -1;
int stdout = -1;
int reversefd = -1;

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
		} else if(strcmp(keystr, "reversefd") == 0) {
			argdata_get_fd(value, &reversefd);
		} else if(strcmp(keystr, "deviceid") == 0) {
			argdata_get_int(value, &device);
		}
		argdata_map_next(&it);
	}

	// reconfigure stderr
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	cosix::reverse_handler *fs = new tmpfs(device);

	dprintf(stdout, "[tmpfs] spawned -- awaiting requests on reverse FD %d\n", reversefd);

	try {
		cosix::handle_requests(reversefd, fs);
	} catch(std::runtime_error &e) {
		dprintf(stdout, "[tmpfs] error: %s\n", e.what());
	}

	delete fs;
	close(reversefd);

	dprintf(stdout, "[tmpfs] closing.\n");
	exit(0);
}
