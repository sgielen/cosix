#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

argdata_t *argdata_create_string(const char *value) {
	return argdata_create_str(value, strlen(value));
}

void start_unittests() {
	int bfd = openat(3, "unittests", O_RDONLY);
	if(bfd < 0) {
		dprintf(0, "Won't run unittests, because I failed to open them: %s\n", strerror(errno));
		return;
	}

	int tmpdir = 3; /* actually bootfs, but we don't have tempdirs yet */
	int logfile = 0; /* vga stream */

	dprintf(0, "Running unit tests...\n");
	argdata_t *keys[] = {argdata_create_string("logfile"), argdata_create_string("tmpdir"), argdata_create_string("nthreads")};
	argdata_t *values[] = {argdata_create_fd(logfile), argdata_create_fd(tmpdir), argdata_create_int(1)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(0, "unittests failed to spawn: %s\n", strerror(errno));
	} else {
		dprintf(0, "unittests spawned, fd: %d\n", pfd);
	}
}

int start_binary(const char *name) {
	int bfd = openat(3, name, O_RDONLY);
	if(bfd < 0) {
		dprintf(0, "Failed to open %s: %s\n", name, strerror(errno));
		return bfd;
	}

	dprintf(0, "Init going to program_spawn() %s...\n", name);

	int pfd = program_spawn(bfd, argdata_create_fd(0));
	if(pfd < 0) {
		dprintf(0, "%s failed to spawn: %s\n", name, strerror(errno));
	} else {
		dprintf(0, "%s spawned, fd: %d\n", name, pfd);
	}

	// wait a while until the process is probably done
	for(int i = 0; i < 500000000; ++i) {}

	return pfd;
}

void program_main(const argdata_t *) {
	dprintf(0, "Init starting up.\n");

	start_binary("exec_test");
	start_binary("thread_test");
	start_binary("pipe_test");

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
