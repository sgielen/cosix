#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>

int stdout = -1;

static void print_argdata_value(const argdata_t *ad, int ) {
	bool bvalue;
	if(argdata_get_bool(ad, &bvalue) == 0) {
		dprintf(stdout, "%s", bvalue ? "true" : "false");
		return;
	}

	int fdvalue;
	if(argdata_get_fd(ad, &fdvalue) == 0) {
		dprintf(stdout, "fd %d", fdvalue);
		return;
	}

	double fvalue;
	if(argdata_get_float(ad, &fvalue) == 0) {
		dprintf(stdout, "%lf", fvalue);
		return;
	}

	int ivalue;
	if(argdata_get_int(ad, &ivalue) == 0) {
		dprintf(stdout, "%d", ivalue);
		return;
	}

	const char *buf;
	size_t len;
	if(argdata_get_str(ad, &buf, &len) == 0) {
		if(strlen(buf) == len) {
			dprintf(stdout, "\"%s\"", buf);
		} else {
			dprintf(stdout, "a buffer of length %d", len);
		}
		return;
	}

	dprintf(stdout, "some unhandled argdata type");
}

static void argdata_dump_map(const argdata_t *ad, int level) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		for(int i = 0; i < level; ++i) {
			dprintf(stdout, "  ");
		}
		dprintf(stdout, "- ");
		print_argdata_value(key, -1);
		dprintf(stdout, " -> ");
		print_argdata_value(value, level);
		dprintf(stdout, "\n");
	}
}

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

	dprintf(stdout, "This is exec_test -- execution succeeded! Parameters received:\n");

	argdata_dump_map(ad, 0);

	exit(0);
}
