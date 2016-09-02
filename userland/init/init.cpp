#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

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
	return pfd;
}

void program_main(const argdata_t *) {
	dprintf(0, "Init starting up.\n");

	start_binary("exec_test");
	start_binary("thread_test");

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
