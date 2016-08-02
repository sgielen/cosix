#include <stdint.h>
#include <stddef.h>

extern "C"
int getpid();

extern "C"
void putstring(const char*, unsigned int len);

extern "C"
int getchar(int offset);

size_t
strlen(const char* str) {
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

void putstring(const char *buf) {
	putstring(buf, strlen(buf));
}

char *ui64toa_s(uint64_t value, char *buffer, size_t bufsize, int base) {
	static const char xlat[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	if(buffer == NULL || bufsize == 0 || base == 0 || base > 36) {
		return NULL;
	}
	size_t i = bufsize;
	buffer[--i] = 0;
	do {
		if(i == 0) {
			return NULL;
		}

		buffer[--i] = xlat[value % base];
	} while(value /= base);

	return buffer + i;
}

extern "C"
void _start() {
	putstring("Hello ");
	putstring("world!\n");

	int pid = getpid();
	char buf[100];
	putstring("This is process ");
	putstring(ui64toa_s(pid, buf, sizeof(buf), 10));
	putstring("!\n");

	size_t len;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(len);
		if(c < 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf, len);

	while(1) {}
}
