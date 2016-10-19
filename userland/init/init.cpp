#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int stdout;
int bootfs;
int reversefd;
int pseudofd;

argdata_t *argdata_create_string(const char *value) {
	return argdata_create_str(value, strlen(value));
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

	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "unittests failed to spawn: %s\n", strerror(errno));
	} else {
		dprintf(stdout, "unittests spawned, fd: %d\n", pfd);
	}
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

int start_binary(const char *name) {
	int bfd = openat(bootfs, name, O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Failed to open %s: %s\n", name, strerror(errno));
		return bfd;
	}

	dprintf(stdout, "Init going to program_spawn() %s...\n", name);

	argdata_t *keys[] = {argdata_create_string("stdout"), argdata_create_string("tmpdir")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(pseudofd)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "%s failed to spawn: %s\n", name, strerror(errno));
	} else {
		dprintf(stdout, "%s spawned, fd: %d\n", name, pfd);
	}

	// wait a while until the process is probably done
	for(int i = 0; i < 500000000; ++i) {}

	return pfd;
}

void program_main(const argdata_t *) {
	stdout = 0;
	bootfs = 3;
	reversefd = 4;
	pseudofd = 5;
	dprintf(stdout, "Init starting up.\n");

	/*start_binary("exec_test");
	start_binary("thread_test");
	start_binary("pipe_test");
	start_binary("concur_test");*/

	start_tmpfs();
	//start_binary("tmptest");

	start_unittests();

	// init must never exit, but we don't have sys_poll() / sys_poll_fd()
	// yet, so we need to busy-wait
	while(1) {}

	// 1. Open the init-binaries directory fd from argdata
	// 2. Read some configuration from the kernel cmdline
	// 3. Start the necessary applications (like dhcpcd)
	// 4. Keep track of their status using poll() / poll_fd()
	//    (so that the application actually always blocks)
	// 5. If needed, open an init RPC socket so that applications or the
	//    kernel can always ask for extra services
}
