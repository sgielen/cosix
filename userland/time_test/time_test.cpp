#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>
#include <cloudabi_syscalls.h>
#include <unistd.h>
#include <time.h>

int stdout = -1;

void print_time() {
	cloudabi_timestamp_t ts = 0;
	cloudabi_sys_clock_time_get(CLOUDABI_CLOCK_MONOTONIC, 0, &ts);
	dprintf(stdout, "Monotonic clock time: %llu\n", ts);
}

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
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	cloudabi_timestamp_t stamp = 0;
	cloudabi_sys_clock_res_get(CLOUDABI_CLOCK_MONOTONIC, &stamp);
	dprintf(stdout, "Monotonic clock resolution: %llu\n", stamp);

	print_time();
	for(int i = 0; i < 500000000; ++i) {}
	print_time();
	for(int i = 0; i < 500000000; ++i) {}
	print_time();

	dprintf(stdout, "Waiting for 5 seconds...\n");
	struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
	print_time();

	dprintf(stdout, "Sleeping for another 5 seconds...\n");
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
	print_time();

	exit(0);
}
