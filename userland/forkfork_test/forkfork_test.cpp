#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>
#include <sys/procdesc.h>
#include <time.h>
#include <unistd.h>

int stdout = -1;

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		}
	}

	int fd;
	int ret = pdfork(&fd);

	// In the child process, fork again
	if(ret == 0) {
		ret = pdfork(&fd);
		if(ret < 0) {
			perror("Inner pdfork failed");
		}
		// We are inner or outer child. Wait forever.
		// TODO: have parent and inner child share a pipe, so that
		// after closing the fd, the parent can do a write on the
		// socket and see if read gives an eof
		while(1) {
			struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
			clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
		}
	}

	// Kill outer child
	close(fd);
	// This should cause all file descriptors of the outer child to be
	// closed, causing the inner child to be killed as well.
	exit(0);
}
