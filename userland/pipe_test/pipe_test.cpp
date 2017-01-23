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

int stdout;

void *thread_handler(void *w) {
	dprintf(stdout, "Sending thread started\n");
	int fd = *reinterpret_cast<int*>(w);
	for(size_t j = 0; j < 5; ++j) {
		// wait a bit
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 /* 200 ms */};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);

		// send something
		char buf[20];
		snprintf(buf, sizeof(buf), "This is message #%d", j + 1);
		dprintf(stdout, "tx: %s\n", buf);
		write(fd, buf, strlen(buf));
	}
	close(fd);
	return nullptr;
}

void program_main(const argdata_t *) {
	stdout = 0;
	dprintf(stdout, "This is pipe_test -- Creating pipe fd's\n");
	int fds[2];
	if(pipe(fds) < 0) {
		dprintf(stdout, "Failed to create pipes: %s\n", strerror(errno));
		exit(0);
	}

	pthread_t thread;
	pthread_create(&thread, NULL, thread_handler, &fds[1]);

	char buf[128];
	for(size_t i = 0; i < 5; ++i) {
		ssize_t count = read(fds[0], buf, sizeof(buf));
		if(count <= 0) {
			dprintf(stdout, "pipe_test: read() failed: %s\n", strerror(errno));
			break;
		}
		buf[count] = 0;
		dprintf(stdout, "rx: %s\n", buf);
	}

	pthread_exit(NULL);
}
