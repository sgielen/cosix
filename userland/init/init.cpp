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
#include <stdint.h>
#include <dirent.h>
#include <vector>
#include <string>

int stdout;
int procfs;
int bootfs;
int reversefd;
int pseudofd;

void allocation_tracker_cmd(char cmd) {
	int alltrackfd = openat(procfs, "kernel/alloctracker", O_WRONLY);
	if(alltrackfd < 0) {
		dprintf(stdout, "INIT: failed to send allocation tracker cmd: %s\n", strerror(errno));
		return;
	}
	write(alltrackfd, &cmd, 1);
	close(alltrackfd);
}

void start_allocation_tracker() {
	allocation_tracker_cmd('1');
}

void stop_allocation_tracker() {
	allocation_tracker_cmd('0');
}

void dump_allocation_tracker() {
	allocation_tracker_cmd('R');
}

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

int program_run(const char *name, int bfd, argdata_t *ad) {
	int pfd = program_spawn(bfd, ad);
	if(pfd < 0) {
		dprintf(stdout, "INIT: %s failed to start: %s\n", name, strerror(errno));
		return -1;
	}

	dprintf(stdout, "INIT: %s started.\n", name);

	siginfo_t si;
	pdwait(pfd, &si, 0);
	dprintf(stdout, "INIT: %s exited, exit status %d\n", name, si.si_status);
	dprintf(stdout, "INIT: current uptime: %ld seconds\n", uptime());

	close(pfd);
	return si.si_status;
}

int start_unittests() {
	int bfd = openat(bootfs, "unittests", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Won't run unittests, because I failed to open them: %s\n", strerror(errno));
		return -1;
	}

	dprintf(stdout, "Running unit tests...\n");
	argdata_t *keys[] = {argdata_create_string("logfile"), argdata_create_string("tmpdir"), argdata_create_string("nthreads")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(pseudofd), argdata_create_int(1)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	auto res = program_run("unittests", bfd, ad);
	close(bfd);
	return res;
}

void start_tmpfs() {
	int bfd = openat(bootfs, "tmpfs", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Can't run tmpfs, because it failed to open: %s\n", strerror(errno));
		return;
	}

	dprintf(stdout, "Running tmpfs...\n");
	argdata_t *keys[] = {argdata_create_string("stdout"), argdata_create_string("reversefd"), argdata_create_string("deviceid")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(reversefd), argdata_create_int(1)};
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
		return 1;
	}

	dprintf(stdout, "Init going to program_spawn() %s...\n", name);

	argdata_t *keys[] = {argdata_create_string("stdout"), argdata_create_string("tmpdir")};
	argdata_t *values[] = {argdata_create_fd(stdout), argdata_create_fd(pseudofd)};
	argdata_t *ad = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	auto r = program_run(name, bfd, ad);
	close(bfd);
	return r;
}

void rm_rf_contents(DIR *d) {
	struct dirent *ent;
	std::vector<std::string> files;
	std::vector<std::string> directories;
	while((ent = readdir(d)) != nullptr) {
		if(ent->d_type == DT_DIR) {
			if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
				directories.push_back(ent->d_name);
			}
		} else {
			files.push_back(ent->d_name);
		}
	}

	for(auto &dir : directories) {
		// delete all files within
		int innerdh = openat(dirfd(d), dir.c_str(), O_RDONLY);
		DIR *innerdir = fdopendir(innerdh);
		rm_rf_contents(innerdir);
		closedir(innerdir);
		unlinkat(dirfd(d), dir.c_str(), AT_REMOVEDIR);
	}
	for(auto &f : files) {
		unlinkat(dirfd(d), f.c_str(), 0);
	}
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
	start_binary("mmap_test");
	start_binary("forkfork_test");

	start_tmpfs();
	//start_binary("tmptest");
	start_binary("unixsock_test");

	uint32_t num_success = 0;
	uint32_t num_failures = 0;
	while(1) {
		auto res = start_unittests();
		if(res == 0) {
			num_success++;
			dprintf(stdout, "== Unittest iteration %d succeeded. Total %d successes, %d failures.\n",
				num_success + num_failures, num_success, num_failures);
		} else {
			num_failures++;
			dprintf(stdout, "== Unittest iteration %d FAILED. Total %d successes, %d failures.\n",
				num_success + num_failures, num_success, num_failures);
		}
		DIR *dir = fdopendir(dup(pseudofd));
		rm_rf_contents(dir);
		closedir(dir);
		struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);

		size_t count = num_success + num_failures;
		if(count == 2) {
			start_allocation_tracker();
		} else if(count == 6) {
			stop_allocation_tracker();
		} else if(count == 10) {
			dump_allocation_tracker();
			break;
		}
	}

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
