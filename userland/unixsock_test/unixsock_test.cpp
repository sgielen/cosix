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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/procdesc.h>

int stdout;
int tmpdir;

void check_same_directory_fd(int a, int b) {
	if(a == b) {
		dprintf(stdout, "[UNIXSOCK] That's cheating!\n");
		exit(1);
	}

	DIR *dir = opendirat(a, "fd_received");
	if(dir != nullptr) {
		dprintf(stdout, "[UNIXSOCK] Directory already existed\n");
		exit(1);
	}

	if(mkdirat(b, "fd_received", 0) < 0) {
		perror("mkdirat");
	}

	dir = opendirat(a, "fd_received");
	if(dir == nullptr) {
		perror("opendirat");
		exit(1);
	}
	closedir(dir);

	if(unlinkat(b, "fd_received", AT_REMOVEDIR) < 0) {
		perror("rmdirat");
	}

	dir = opendirat(a, "fd_received");
	if(dir != nullptr) {
		dprintf(stdout, "[UNIXSOCK] Directory still exists\n");
		exit(1);
	}
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
		else if(strcmp(keystr, "tmpdir") == 0) {
			argdata_get_fd(value, &tmpdir);
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	dprintf(stdout, "[UNIXSOCK] Creating SOCK_DGRAM socketpair\n");
	int fds[2];
	{
		if(socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) < 0) {
			perror("socketpair");
		}
		if(write(fds[0], "foo", 3) != 3) {
			perror("write");
		}

		char buf[10];
		if(read(fds[1], buf, sizeof(buf)) != 3) {
			perror("read");
		}
		if(strncmp(buf, "foo", 3) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message is different!\n");
			exit(1);
		}

		if(write(fds[0], "foo", 3) != 3) {
			perror("write 2");
		}
		if(write(fds[0], "bar", 3) != 3) {
			perror("write 3");
		}

		if(read(fds[1], buf, sizeof(buf)) != 3) {
			perror("read 2");
		}
		if(strncmp(buf, "foo", 3) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message 2 is different!\n");
			exit(1);
		}
		if(read(fds[1], buf, sizeof(buf)) != 3) {
			perror("read 3");
		}
		if(strncmp(buf, "bar", 3) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message 3 is different!\n");
			exit(1);
		}
	}

	{
		struct iovec iov[2] = {{.iov_base = const_cast<void*>(reinterpret_cast<const void*>("foob")), .iov_len=4},
				       {.iov_base = const_cast<void*>(reinterpret_cast<const void*>("ar")), .iov_len=2}};
		alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
		struct msghdr message = {
			.msg_iov = iov,
			.msg_iovlen = 2,
			.msg_control = control,
			.msg_controllen = sizeof(control),
			.msg_flags = 0,
		};
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
		fdbuf[0] = tmpdir;
		if(sendmsg(fds[1], &message, 0) != 6) {
			perror("sendmsg");
		}
	}

	{
		char strings[3][2];
		struct iovec iov[3] = {
			{.iov_base = strings[0], .iov_len = sizeof(strings[0])},
			{.iov_base = strings[1], .iov_len = sizeof(strings[1])},
			{.iov_base = strings[2], .iov_len = sizeof(strings[2])}};
		alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
		struct msghdr message = {
			.msg_iov =iov,
			.msg_iovlen = 3,
			.msg_control = control,
			.msg_controllen = sizeof(control),
			.msg_flags = 0,
		};
		if(recvmsg(fds[0], &message, 0) != 6) {
			perror("recvmsg");
		}
		if(strncmp(strings[0], "fo", 2) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message 4.1 is different!\n");
			exit(1);
		}
		if(strncmp(strings[1], "ob", 2) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message 4.2 is different!\n");
			exit(1);
		}
		if(strncmp(strings[2], "ar", 2) != 0) {
			dprintf(stdout, "[UNIXSOCK] Received message 4.3 is different!\n");
			exit(1);
		}

		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
		if(cmsg == nullptr) {
			dprintf(stdout, "[UNIXSOCK] No fds attached to message 4!\n");
			exit(1);
		}
		if(cmsg->cmsg_len != CMSG_LEN(sizeof(int)) || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
			dprintf(stdout, "[UNIXSOCK] Invalid fds attached to message 4!\n");
		}
		int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
		check_same_directory_fd(tmpdir, fdbuf[0]);
		close(fdbuf[0]);
	}
	close(fds[0]);
	close(fds[1]);

	dprintf(stdout, "[UNIXSOCK] SOCK_DGRAM test completed!\n");

	dprintf(stdout, "[UNIXSOCK] Performing on-filesystem SOCK_STREAM test\n");
	{
		int fd1 = socket(AF_UNIX, SOCK_STREAM, 0);
		if(fd1 < 0) {
			perror("socket");
			fflush(stderr);
		}
		if(bindat(fd1, tmpdir, "sock") < 0) {
			perror("bindat");
			fflush(stderr);
		}
		if(listen(fd1, SOMAXCONN) < 0) {
			perror("listen");
			fflush(stderr);
		}
		int pfd;
		int ret = pdfork(&pfd);
		if(ret == 0) {
			// I'm the child
			int fd2 = socket(AF_UNIX, SOCK_STREAM, 0);
			if(fd2 < 0) {
				perror("socket2");
				exit(1);
			}
			if(connectat(fd2, tmpdir, "sock") < 0) {
				perror("connect");
				exit(1);
			}
			if(write(fd2, "Hello ", 6) != 6) {
				perror("write1");
				exit(1);
			}
			if(write(fd2, "world!", 6) != 6) {
				perror("write2");
				exit(1);
			}
			shutdown(fd2, SHUT_RDWR);
			close(fd1);
			close(fd2);
			exit(0);
		}
		int fd3 = accept(fd1, NULL, NULL);
		if(fd3 < 0) {
			perror("accept");
			fflush(stderr);
		}
		siginfo_t si;
		pdwait(pfd, &si, 0);
		if(si.si_status != 0) {
			dprintf(stdout, "[UNIXSOCK] child died with status %d\n", si.si_status);
		}
		close(pfd);
		char buf[13];
		if(read(fd3, buf, sizeof(buf)) != 12) {
			perror("read");
			fflush(stderr);
		}
		buf[12] = 0;
		if(strcmp(buf, "Hello world!") != 0) {
			dprintf(stdout, "[UNIXSOCK] Messages through UNIXSOCK aren't the same! read \"%s\"\n", buf);
		}
		close(fd1);
		close(fd3);
	}
	dprintf(stdout, "[UNIXSOCK] On-filesystem test completed!\n");

	exit(0);
}
