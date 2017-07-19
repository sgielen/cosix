#include <oslibc/ctype.h>

int
isupper(int c) {
	return (c >= 0x41 && c <= 0x5a) ? 1 : 0;
}

int
islower(int c) {
	return (c >= 0x61 && c <= 0x7a) ? 1 : 0;
}

int
isalpha(int c) {
	return (isupper(c) || islower(c)) ? 1 : 0;
}

int
isdigit(int c) {
	return (c >= 0x30 && c <= 0x39) ? 1 : 0;
}
