#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <sys/procdesc.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

int stdout;
int procfs;
int bootfs;
int reversefd;
int pseudofd;

long uptime() {
	int uptimefd = openat(procfs, "kernel/uptime", O_RDONLY);
	if(uptimefd < 0) {
		dprintf(stdout, "INIT: failed to open uptime: %s\n", strerror(errno));
		return 0;
	}
	char buf[16];
	ssize_t r = read(uptimefd, buf, sizeof(buf) - 1);
	if(r <= 0) {
		dprintf(stdout, "INIT: failed to read uptime: %s\n", strerror(errno));
		return 0;
	}
	buf[r] = 0;
	close(uptimefd);
	return atol(buf);
}

argdata_t *argdata_create_string(const char *value) {
	return argdata_create_str(value, strlen(value));
}

void program_run(const char *name, int bfd, argdata_t *ad) {
	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "INIT: %s failed to start: %s\n", name, strerror(errno));
		return;
	}

	dprintf(stdout, "INIT: %s started.\n", name);

	siginfo_t si;
	pdwait(pfd, &si, 0);
	dprintf(stdout, "INIT: %s exited, exit status %d\n", name, si.si_status);
	dprintf(stdout, "INIT: current uptime: %ld seconds\n", uptime());

	close(pfd);
}

void start_unittests() {
	int bfd = openat(bootfs, "unittests", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Won't run unittests, because I failed to open them: %s\n", strerror(errno));
		return;
	}

	dprintf(stdout, "Running unit tests...\n");
	argdata_t *keys[] = {argdata_create_string("logfile"), argdata_create_string("tmpdir"), argdata_create_string("nthreads")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(pseudofd), argdata_create_int(1)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	program_run("unittests", bfd, ad);
	close(bfd);
}

void start_tmpfs() {
	int bfd = openat(bootfs, "tmpfs", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Can't run tmpfs, because it failed to open: %s\n", strerror(errno));
		return;
	}

	dprintf(stdout, "Running tmpfs...\n");
	argdata_t *keys[] = {argdata_create_string("stdout"), argdata_create_string("reversefd")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(reversefd)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "tmpfs failed to spawn: %s\n", strerror(errno));
	} else {
		dprintf(stdout, "tmpfs spawned, fd: %d\n", pfd);
	}
}

void start_binary(const char *name) {
	int bfd = openat(bootfs, name, O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Failed to open %s: %s\n", name, strerror(errno));
		return;
	}

	dprintf(stdout, "Init going to program_spawn() %s...\n", name);

	argdata_t *keys[] = {argdata_create_string("stdout"), argdata_create_string("tmpdir")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(pseudofd)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	program_run(name, bfd, ad);
	close(bfd);
}

void program_main(const argdata_t *) {
	stdout = 0;
	procfs = 2;
	bootfs = 3;
	reversefd = 4;
	pseudofd = 5;
	dprintf(stdout, "Init starting up.\n");

	// reconfigure stderr
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	start_binary("exec_test");
	start_binary("thread_test");
	start_binary("pipe_test");
	start_binary("concur_test");
	start_binary("time_test");

	start_tmpfs();
	//start_binary("tmptest");

	start_unittests();

	pthread_mutex_t mtx;
	pthread_mutex_init(&mtx, NULL);
	pthread_cond_t cond;
	pthread_cond_init(&cond, NULL);
	pthread_mutex_lock(&mtx);
	pthread_cond_wait(&cond, &mtx);
	pthread_mutex_unlock(&mtx);
	exit(0);

	// 1. Open the init-binaries directory fd from argdata
	// 2. Read some configuration from the kernel cmdline
	// 3. Start the necessary applications (like dhcpcd)
	// 4. Keep track of their status using poll() / poll_fd()
	//    (so that the application actually always blocks)
	// 5. If needed, open an init RPC socket so that applications or the
	//    kernel can always ask for extra services
}
