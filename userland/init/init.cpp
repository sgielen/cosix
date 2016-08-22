#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

void program_main(const argdata_t *) {
	dprintf(0, "Init starting up.\n");

	int exec_test_fd = openat(3, "exec_test", O_RDONLY);
	if(exec_test_fd < 0) {
		dprintf(0, "Failed to open exec_test: %s\n", strerror(errno));
		exit(1);
	}

	dprintf(0, "Init going to program_spawn() exec_test...\n");

	int fd = program_spawn(exec_test_fd, argdata_create_fd(0));
	if(fd < 0) {
		dprintf(0, "exec_test failed to spawn: %s\n", strerror(errno));
	} else {
		dprintf(0, "exec_test spawned, fd: %d\n", fd);
	}

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
