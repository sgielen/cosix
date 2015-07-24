#include "process.hpp"
#include "oslibc/string.h"
#include "oslibc/numeric.h"

void putstring(const char *str) {
	putstring(str, strlen(str));
}

#define PROC_WAITTIME SIZE_MAX / 500
static void wait() {
	volatile size_t i = 0;
	for(; i < PROC_WAITTIME; ++i) {
		// do nothing
	}
}

void process_main() {
	putstring("Process ");
	char buf[64];
	putstring(ui64toa_s(getpid(), &buf[0], sizeof(buf), 10));
	putstring(" started!\n");

	while(1) {
		putstring("This is process ");
		putstring(ui64toa_s(getpid(), &buf[0], sizeof(buf), 10));
		putstring(".\n");
		wait();
	}
}
