#include <stdint.h>
#include <stddef.h>

extern "C"
int getpid();

extern "C"
void putstring(const char*, unsigned int len);

extern "C"
int getchar(int fd, int offset);

extern "C"
int openat(int fd, const char*, int directory);

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

char *i64toa_s(int64_t value, char *buffer, size_t bufsize, int base) {
	uint8_t neg = value < 0;
	if(neg) value = -value;
	char *b = ui64toa_s((uint64_t)value, buffer, bufsize, base);
	if(b == NULL || (b == buffer && neg)) {
		return NULL;
	}
	if(neg) {
		b -= 1;
		b[0] = '-';
	}
	return b;
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
		int c = getchar(1, len);
		if(c <= 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf, len);

	/* Read procfs/kernel/uptime */
	int c_ = getchar(2, 0);
	putstring("Got return value of procfs getchar:");
	putstring(i64toa_s(c_, buf, sizeof(buf), 10));
	putstring(".\n");

	int dirfd = openat(2, "kernel", 1);
	putstring("openat(2, \"kernel\", 1) = ");
	putstring(i64toa_s(dirfd, buf, sizeof(buf), 10));
	putstring("\n");

	int fd = openat(2, "kernel", 0);
	putstring("openat(2, \"kernel\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	fd = openat(2, "kernel/uptime", 1);
	putstring("openat(2, \"kernel/uptime\", 1) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	fd = openat(2, "kernel/uptime", 0);
	putstring("openat(2, \"kernel/uptime\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	putstring("Contents of fd(2)/kernel/uptime: ");
	len = 0;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(fd, len);
		if(c < 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf);
	putstring("\n");

	fd = openat(dirfd, "uptime", 0);
	putstring("openat(dirfd, \"uptime\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	putstring("Contents of fd(dirfd)/uptime: ");
	len = 0;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(fd, len);
		if(c < 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf);
	putstring("\n");

	while(1) {}
}
