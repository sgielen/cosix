#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <argdata.hpp>
#include <cloudabi_syscalls.h>
#include <thread>
#include <dirent.h>

#include <arpa/inet.h>
#include <mstd/range.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <string>

int terminal;
int tmpdir;
int initrd;
int networkd;
int procfs;
int bootfs;

void parse_params(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	terminal = -1;
	networkd = -1;
	procfs = -1;
	bootfs = -1;
	tmpdir = -1;
	initrd = -1;
	while (argdata_map_get(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			argdata_map_next(&it);
			continue;
		}

		if(strcmp(keystr, "terminal") == 0) {
			argdata_get_fd(value, &terminal);
		} else if(strcmp(keystr, "networkd") == 0) {
			argdata_get_fd(value, &networkd);
		} else if(strcmp(keystr, "procfs") == 0) {
			argdata_get_fd(value, &procfs);
		} else if(strcmp(keystr, "bootfs") == 0) {
			argdata_get_fd(value, &bootfs);
		} else if(strcmp(keystr, "tmpdir") == 0) {
			argdata_get_fd(value, &tmpdir);
		} else if(strcmp(keystr, "initrd") == 0) {
			argdata_get_fd(value, &initrd);
		}
		argdata_map_next(&it);
	}
}

void program_main(const argdata_t *ad) {
	parse_params(ad);

	FILE *out = fdopen(terminal, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	// Find python on the initrd
	int bfd = openat(initrd, "bin/python3.6", O_RDONLY);
	if(bfd < 0) {
		fprintf(stderr, "Won't run Python shell, because I failed to open it: %s\n", strerror(errno));
		exit(1);
	}

	// Find python libs on the initrd
	int libfd = openat(initrd, "lib/python3.6", O_RDONLY);
	if(libfd < 0) {
		fprintf(stderr, "Won't run Python shell, because I failed to open the libdir: %s\n", strerror(errno));
		exit(1);
	}

	// Find cosix libs on the initrd
	int clibfd = openat(initrd, "lib/cosix", O_RDONLY);
	if(clibfd < 0) {
		fprintf(stderr, "Won't run Python shell, because I failed to open the Cosix libdir: %s\n", strerror(errno));
		exit(1);
	}

	std::unique_ptr<argdata_t> paths[] = {
		argdata_t::create_fd(libfd),
		argdata_t::create_fd(clibfd)
	};
	std::vector<argdata_t*> path_ptrs;
	for(auto &path : mstd::range<std::unique_ptr<argdata_t>>(paths)) {
		path_ptrs.push_back(path.get());
	}

	std::unique_ptr<argdata_t> args_keys[] = {
		argdata_t::create_str("procfs"),
		argdata_t::create_str("bootfs"),
		argdata_t::create_str("tmpdir"),
		argdata_t::create_str("networkd"),
		argdata_t::create_str("terminal"),
	};
	std::unique_ptr<argdata_t> args_values[] = {
		argdata_t::create_fd(procfs),
		argdata_t::create_fd(bootfs),
		argdata_t::create_fd(tmpdir),
		argdata_t::create_fd(networkd),
		argdata_t::create_fd(terminal),
	};
	std::vector<argdata_t*> args_key_ptrs;
	std::vector<argdata_t*> args_value_ptrs;
	
	for(auto &key : mstd::range<std::unique_ptr<argdata_t>>(args_keys)) {
		args_key_ptrs.push_back(key.get());
	}
	for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(args_values)) {
		args_value_ptrs.push_back(value.get());
	}

	std::string command = R"PYTHON(
import io
import sys
import traceback
from cosix import *

sys.stdout = sys.stderr
sys.terminal = sys.argdata['terminal']

try:
  cons = SockConsole(sys.terminal, globals())
  cons.runsource("from cosix import *", "<init>")
  cons.interact()
except TerminalClosedError:
  pass
except ConnectionError:
  pass
except Exception as e:
  traceback.print_exc(file=sys.stderr)

sys.stdout.flush()
sys.terminal.flush()
)PYTHON";

	std::unique_ptr<argdata_t> keys[] =
		{argdata_t::create_str("stderr"), argdata_t::create_str("path"),
		argdata_t::create_str("args"), argdata_t::create_str("command")};
	std::unique_ptr<argdata_t> values[] =
		{argdata_t::create_fd(terminal), argdata_t::create_seq(path_ptrs),
		 argdata_t::create_map(args_key_ptrs, args_value_ptrs), argdata_t::create_str(command.c_str())};
	std::vector<argdata_t*> key_ptrs;
	std::vector<argdata_t*> value_ptrs;
	
	for(auto &key : mstd::range<std::unique_ptr<argdata_t>>(keys)) {
		key_ptrs.push_back(key.get());
	}
	for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(values)) {
		value_ptrs.push_back(value.get());
	}
	auto python_ad = argdata_t::create_map(key_ptrs, value_ptrs);

	int res = program_exec(bfd, python_ad.get());
	fprintf(stderr, "Failed to spawn python: %s\n", strerror(res));
	exit(1);
}
