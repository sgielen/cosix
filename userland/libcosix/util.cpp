#include <cosix/util.hpp>

#include <assert.h>
#include <errno.h>
#include <program.h>
#include <signal.h>
#include <sys/socket.h>

static void spawned(uv_process_t *handle, int64_t exit_status, int term_signal) {
	auto *pd2 = static_cast<cosix::procdesc2*>(handle->data);

	pd2->terminated = true;
	pd2->exit_status = exit_status;
	pd2->term_signal = term_signal;
}

cosix::procdesc2 *cosix::program_spawn2(int binary, argdata_t const *argdata) {
	procdesc2 *pd2 = new procdesc2;
	pd2->loop = new uv_loop_t;
	uv_loop_init(pd2->loop);

	pd2->handle = new uv_process_t;
	pd2->handle->data = pd2;

	pd2->terminated = false;

	auto res = program_spawn(pd2->loop, pd2->handle, binary, argdata, spawned);
	if(res != 0) {
		delete pd2->handle;
		uv_loop_close(pd2->loop);
		delete pd2->loop;
		delete pd2;

		errno = res;
		return nullptr;
	}

	uv_run(pd2->loop, UV_RUN_NOWAIT);
	return pd2;
}

void cosix::program_kill2(procdesc2 *pd2) {
	uv_process_kill(pd2->handle, SIGKILL);
	program_wait2(pd2, nullptr, nullptr);
}

void cosix::program_wait2(procdesc2 *pd2, int64_t *exit_status, int *term_signal) {
	uv_run(pd2->loop, UV_RUN_DEFAULT);
	assert(pd2->terminated);

	uv_close((uv_handle_t*)pd2->handle, nullptr);
	delete pd2->handle;

	uv_loop_close(pd2->loop);
	delete pd2->loop;

	if(exit_status) *exit_status = pd2->exit_status;
	if(term_signal) *term_signal = pd2->term_signal;

	delete pd2;
}

ssize_t cosix::read_response_and_fd(int sock, char *buf, size_t bufsize, int &fd)
{
	int ignore;
	return read_response_and_fd2(sock, buf, bufsize, fd, ignore);
}

ssize_t cosix::read_response_and_fd2(int sock, char *buf, size_t bufsize, int &fd1, int &fd2)
{
	struct iovec iov = {.iov_base = buf, .iov_len = bufsize};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(sock, &msg, 0);
	if(size < 0) {
		fd1 = -1;
		fd2 = -1;
		return size;
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET) {
		fd1 = -1;
		fd2 = -1;
		return size;
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	if(cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
		fd1 = fdbuf[0];
		fd2 = -1;
	} else if(cmsg->cmsg_len == CMSG_LEN(sizeof(int) * 2)) {
		fd1 = fdbuf[0];
		fd2 = fdbuf[1];
	} else {
		fd1 = -1;
		fd2 = -1;
	}
	return size;
}
