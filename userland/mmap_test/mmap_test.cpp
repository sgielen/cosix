#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>
#include <cloudabi_syscalls.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

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

	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	errno = 0;
	unsigned char *addr = reinterpret_cast<unsigned char*>(mmap(0, 4096 * 4, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, CLOUDABI_MAP_ANON_FD, 0));
	if(addr == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}

	for(size_t i = 0; i < 4096 * 4; ++i) {
		addr[i] = 0xba;
	}

	void *addr2 = reinterpret_cast<void*>(addr + 4096 * 2);
	if(addr2 != mmap(addr2, 4096, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, CLOUDABI_MAP_ANON_FD, 0)) {
		perror("Overwriting mmap failed");
		exit(1);
	}

	bool ok = true;
	size_t i;
	for(i = 0; i < 4096 * 2; ++i) {
		if(addr[i] != 0xba) {
			fprintf(stderr, "First two pages are unexpected\n");
			ok = false;
			break;
		}
	}
	for(i = 4096 * 2; i < 4096 * 3; ++i) {
		if(addr[i] != 0x00) {
			fprintf(stderr, "Third page is unexpected\n");
			ok = false;
			break;
		}
		addr[i] = 0xf0;
	}
	for(i = 4096 * 3; i < 4096 * 4; ++i) {
		if(addr[i] != 0xba) {
			fprintf(stderr, "Fourth page is unexpected\n");
			ok = false;
			break;
		}
	}

	if(!ok) {
		exit(1);
	}

	void *addr3 = reinterpret_cast<void*>(addr + 4096 * 3);
	if(addr3 != mmap(addr3, 4096, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, CLOUDABI_MAP_ANON_FD, 0)) {
		perror("Overwriting mmap 2 failed");
		exit(1);
	}

	for(i = 0; i < 4096 * 2; ++i) {
		if(addr[i] != 0xba) {
			fprintf(stderr, "First two pages are unexpected\n");
			ok = false;
			break;
		}
	}
	for(i = 4096 * 2; i < 4096 * 3; ++i) {
		if(addr[i] != 0xf0) {
			fprintf(stderr, "Third page is unexpected");
			ok = false;
			break;
		}
	}
	for(i = 4096 * 3; i < 4096 * 4; ++i) {
		if(addr[i] != 0x00) {
			fprintf(stderr, "Fourth page is unexpected\n");
			ok = false;
			break;
		}
		addr[i] = 0x55;
	}

	if(!ok) {
		exit(1);
	}

	if(addr != mmap(addr, 4096 * 2, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, CLOUDABI_MAP_ANON_FD, 0)) {
		perror("Overwriting mmap 3 failed");
		exit(1);
	}

	for(i = 0; i < 4096 * 2; ++i) {
		if(addr[i] != 0x00) {
			fprintf(stderr, "First two pages are unexpected\n");
			ok = false;
			break;
		}
	}
	for(i = 4096 * 2; i < 4096 * 3; ++i) {
		if(addr[i] != 0xf0) {
			fprintf(stderr, "Third page is unexpected");
			ok = false;
			break;
		}
	}
	for(i = 4096 * 3; i < 4096 * 4; ++i) {
		if(addr[i] != 0x55) {
			fprintf(stderr, "Fourth page is unexpected\n");
			ok = false;
			break;
		}
	}

	if(!ok) {
		exit(1);
	}
	fprintf(stderr, "All seems fine!\n");
	exit(0);
}
