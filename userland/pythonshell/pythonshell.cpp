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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/procdesc.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int stdout;
int tmpdir;
int initrd;
int networkd;
int procfs;
int bootfs;

int networkd_get_socket(int type, std::string connect, std::string bind) {
	std::string command;
	if(type == SOCK_DGRAM) {
		command = "udpsock";
	} else if(type == SOCK_STREAM) {
		command = "tcpsock";
	} else {
		throw std::runtime_error("Unknown type of socket to get");
	}

	std::unique_ptr<argdata_t> keys[] =
		{argdata_t::create_str("command"), argdata_t::create_str("connect"), argdata_t::create_str("bind")};
	std::unique_ptr<argdata_t> values[] =
		{argdata_t::create_str(command.c_str()), argdata_t::create_str(connect.c_str()), argdata_t::create_str(bind.c_str())};
	std::vector<argdata_t*> key_ptrs;
	std::vector<argdata_t*> value_ptrs;
	
	for(auto &key : mstd::range<std::unique_ptr<argdata_t>>(keys)) {
		key_ptrs.push_back(key.get());
	}
	for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(values)) {
		value_ptrs.push_back(value.get());
	}
	auto map = argdata_t::create_map(key_ptrs, value_ptrs);

	std::vector<unsigned char> rbuf;
	map->serialize(rbuf);

	write(networkd, rbuf.data(), rbuf.size());
	// TODO: set non-blocking flag once kernel supports it
	// this way, we can read until EOF instead of only 200 bytes
	// TODO: for a generic implementation, MSG_PEEK to find the number
	// of file descriptors
	uint8_t buf[1500];
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(networkd, &msg, 0);
	if(size < 0) {
		perror("read");
		exit(1);
	}
	auto response = argdata_t::create_from_buffer(mstd::range<unsigned char const>(&buf[0], size));
	int fdnum = -1;
	for(auto i : response->as_map()) {
		auto key = i.first->as_str();
		if(key == "error") {
			throw std::runtime_error("Failed to retrieve TCP socket from networkd: " + i.second->as_str().to_string());
		} else if(key == "fd") {
			fdnum = *i.second->get_fd();
		}
	}
	if(fdnum != 0) {
		throw std::runtime_error("Ifstore TCP socket not received");
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
		dprintf(stdout, "Ifstore socket requested, but not given\n");
		exit(1);
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	return fdbuf[0];
}

void parse_params(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	stdout = -1;
	networkd = -1;
	procfs = -1;
	bootfs = -1;
	tmpdir = -1;
	initrd = -1;
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
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
	}
}

void program_main(const argdata_t *ad) {
	parse_params(ad);

	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	// Listen socket: bound to 0.0.0.0:26, listening
	int listensock = networkd_get_socket(SOCK_STREAM, "", "0.0.0.0:26");

	// Find python on the initrd
	int bfd = openat(initrd, "bin/python3.6", O_RDONLY);
	if(bfd < 0) {
		dprintf(stdout, "Won't run Python shell, because I failed to open it: %s\n", strerror(errno));
		exit(1);
	}

	// Find python libs on the initrd
	int libfd = openat(initrd, "lib/python3.6", O_RDONLY);
	if(libfd < 0) {
		dprintf(stdout, "Won't run Python shell, because I failed to open the libdir: %s\n", strerror(errno));
		exit(1);
	}

	// Find cosix libs on the initrd
	int clibfd = openat(initrd, "lib/cosix", O_RDONLY);
	if(clibfd < 0) {
		dprintf(stdout, "Won't run Python shell, because I failed to open the Cosix libdir: %s\n", strerror(errno));
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
		argdata_t::create_str("socket"),
		argdata_t::create_str("procfs"),
		argdata_t::create_str("bootfs"),
		argdata_t::create_str("tmpdir"),
		argdata_t::create_str("networkd")
	};
	std::unique_ptr<argdata_t> args_values[] = {
		argdata_t::create_fd(listensock),
		argdata_t::create_fd(procfs),
		argdata_t::create_fd(bootfs),
		argdata_t::create_fd(tmpdir),
		argdata_t::create_fd(networkd),
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
from cosix import *

print("Python shell started!")
sys.stderr.flush()

class Output(io.IOBase):
  def __init__(self, file):
    self.file = file
    self.sock = None

  def fileno(self):
    return self.file.fileno()

  def write(self, string):
    self.file.write(string)
    self.file.flush()
    if self.sock is not None:
      try:
        self.sock.send(bytearray(string, "UTF-8"))
      except Exception as e:
        self.sock = None
        self.write("Failed to write string to socket: " + str(e) + "\n")
        raise

listensock = sys.argdata['socket']
output = Output(sys.stderr)
sys.stdout = output
sys.stderr = output
while listensock:
  (conn, address) = listensock.accept()
  file = conn.makefile('w')
  output.sock = conn
  try:
    cons = SockConsole(conn, globals())
    cons.runsource("from cosix import *", "<init>")
    cons.interact()
  except SocketClosedError:
    pass
  except Exception as e:
    output.write("Exception occurred: " + str(e) + "\n")
  output.sock = None
  conn.close()
)PYTHON";

	std::unique_ptr<argdata_t> keys[] =
		{argdata_t::create_str("stderr"), argdata_t::create_str("path"),
		argdata_t::create_str("args"), argdata_t::create_str("command")};
	std::unique_ptr<argdata_t> values[] =
		{argdata_t::create_fd(stdout), argdata_t::create_seq(path_ptrs),
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
	dprintf(stdout, "Failed to spawn python: %s\n", strerror(res));
	exit(1);
}
