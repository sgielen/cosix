#include <cloudabi/headers/cloudabi_types.h>

static void putstring(const char *str, unsigned int len)
{
	register uint32_t reg_eax asm("eax") = 2;
	register const char *reg_ecx asm("ecx") = str;
	register uint32_t reg_edx asm("edx") = len;
	register uint32_t reg_ebx asm("ebx") = 0;
	asm volatile("int $0x80"
		: : "r"(reg_eax), "r"(reg_ecx), "r"(reg_edx), "r"(reg_ebx)
		: "memory", "eax", "ecx", "edx", "ebx");
}

static size_t
strlen(const char* str) {
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

static void putstring_l(const char *buf) {
	putstring(buf, strlen(buf));
	putstring("\n", 1);
}

cloudabi_errno_t
cloudabi_sys_clock_res_get(
	cloudabi_clockid_t clock_id,
	cloudabi_timestamp_t *resolution
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_clock_time_get(
	cloudabi_clockid_t clock_id,
	cloudabi_timestamp_t precision,
	cloudabi_timestamp_t *time
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_condvar_signal(
	_Atomic(cloudabi_condvar_t) *condvar,
	cloudabi_scope_t scope,
	cloudabi_nthreads_t nwaiters
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_close(
	cloudabi_fd_t fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_create1(
	cloudabi_filetype_t type,
	cloudabi_fd_t *fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_create2(
	cloudabi_filetype_t type,
	cloudabi_fd_t *fd1,
	cloudabi_fd_t *fd2
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_datasync(
	cloudabi_fd_t fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_dup(
	cloudabi_fd_t from,
	cloudabi_fd_t *fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_pread(
	cloudabi_fd_t fd,
	const cloudabi_iovec_t *iov,
	size_t iovcnt,
	cloudabi_filesize_t offset,
	size_t *nread
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_pwrite(
	cloudabi_fd_t fd,
	const cloudabi_ciovec_t *iov,
	size_t iovcnt,
	cloudabi_filesize_t offset,
	size_t *nwritten
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_read(
	cloudabi_fd_t fd,
	const cloudabi_iovec_t *iov,
	size_t iovcnt,
	size_t *nread
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_replace(
	cloudabi_fd_t from,
	cloudabi_fd_t to
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_seek(
	cloudabi_fd_t fd,
	cloudabi_filedelta_t offset,
	cloudabi_whence_t whence,
	cloudabi_filesize_t *newoffset
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_stat_get(
	cloudabi_fd_t fd,
	cloudabi_fdstat_t *buf
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_stat_put(
	cloudabi_fd_t fd,
	const cloudabi_fdstat_t *buf,
	cloudabi_fdsflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_sync(
	cloudabi_fd_t fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_fd_write(
	cloudabi_fd_t fd,
	const cloudabi_ciovec_t *iov,
	size_t iovcnt,
	size_t *nwritten
) {
	register uint32_t reg_eax asm("eax") = 2;
	register uint32_t reg_ebx asm("ebx") = fd;
	register const char *reg_ecx asm("ecx");
	register uint32_t reg_edx asm("edx");
	*nwritten = 0;
	for(size_t i = 0; i < iovcnt; ++i) {
		reg_ecx = iov[i].iov_base;
		reg_edx = iov[i].iov_len;
		asm volatile("int $0x80"
			: : "r"(reg_eax), "r"(reg_ebx), "r"(reg_ecx), "r"(reg_edx)
			: "memory", "eax", "ebx", "ecx", "edx");
		*nwritten += reg_edx;
	}
	return 0;
}

cloudabi_errno_t
cloudabi_sys_file_advise(
	cloudabi_fd_t fd,
	cloudabi_filesize_t offset,
	cloudabi_filesize_t len,
	cloudabi_advice_t advice
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_allocate(
	cloudabi_fd_t fd,
	cloudabi_filesize_t offset,
	cloudabi_filesize_t len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_create(
	cloudabi_fd_t fd,
	const char *path,
	size_t pathlen,
	cloudabi_filetype_t type
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_link(
	cloudabi_lookup_t fd1,
	const char *path1,
	size_t path1len,
	cloudabi_fd_t fd2,
	const char *path2,
	size_t path2len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_open(
	cloudabi_lookup_t dirfd,
	const char *path,
	size_t pathlen,
	cloudabi_oflags_t oflags,
	const cloudabi_fdstat_t *fds,
	cloudabi_fd_t *fd
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_readdir(
	cloudabi_fd_t fd,
	void *buf,
	size_t nbyte,
	cloudabi_dircookie_t cookie,
	size_t *bufused
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_readlink(
	cloudabi_fd_t fd,
	const char *path,
	size_t pathlen,
	char *buf,
	size_t bufsize,
	size_t *bufused
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_rename(
	cloudabi_fd_t oldfd,
	const char *old,
	size_t oldlen,
	cloudabi_fd_t newfd,
	const char *new,
	size_t newlen
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_stat_fget(
	cloudabi_fd_t fd,
	cloudabi_filestat_t *buf
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_stat_fput(
	cloudabi_fd_t fd,
	const cloudabi_filestat_t *buf,
	cloudabi_fsflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_stat_get(
	cloudabi_lookup_t fd,
	const char *path,
	size_t pathlen,
	cloudabi_filestat_t *buf
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_stat_put(
	cloudabi_lookup_t fd,
	const char *path,
	size_t pathlen,
	const cloudabi_filestat_t *buf,
	cloudabi_fsflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_symlink(
	const char *path1,
	size_t path1len,
	cloudabi_fd_t fd,
	const char *path2,
	size_t path2len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_file_unlink(
	cloudabi_fd_t fd,
	const char *path,
	size_t pathlen,
	cloudabi_ulflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_lock_unlock(
	_Atomic(cloudabi_lock_t) *lock,
	cloudabi_scope_t scope
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_advise(
	void *addr,
	size_t len,
	cloudabi_advice_t advice
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_lock(
	const void *addr,
	size_t len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_map(
	void *addr,
	size_t len,
	cloudabi_mprot_t prot,
	cloudabi_mflags_t flags,
	cloudabi_fd_t fd,
	cloudabi_filesize_t off,
	void **mem
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_protect(
	void *addr,
	size_t len,
	cloudabi_mprot_t prot
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_sync(
	void *addr,
	size_t len,
	cloudabi_msflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_unlock(
	const void *addr,
	size_t len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_mem_unmap(
	void *addr,
	size_t len
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_poll(
	const cloudabi_subscription_t *in,
	cloudabi_event_t *out,
	size_t nsubscriptions,
	size_t *nevents
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_poll_fd(
	cloudabi_fd_t fd,
	const cloudabi_subscription_t *in,
	size_t nin,
	cloudabi_event_t *out,
	size_t nout,
	const cloudabi_subscription_t *timeout,
	size_t *nevents
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_proc_exec(
	cloudabi_fd_t fd,
	const void *data,
	size_t datalen,
	const cloudabi_fd_t *fds,
	size_t fdslen
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

_Noreturn void
cloudabi_sys_proc_exit(
	cloudabi_exitcode_t rval
) {
	putstring_l(__PRETTY_FUNCTION__);
	while(1) {}
}

cloudabi_errno_t
cloudabi_sys_proc_raise(
	cloudabi_signal_t sig
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_random_get(
	void *buf,
	size_t nbyte
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_accept(
	cloudabi_fd_t sock,
	cloudabi_sockstat_t *buf,
	cloudabi_fd_t *conn
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_bind(
	cloudabi_fd_t sock,
	cloudabi_fd_t fd,
	const char *path,
	size_t pathlen
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_connect(
	cloudabi_fd_t sock,
	cloudabi_fd_t fd,
	const char *path,
	size_t pathlen
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_listen(
	cloudabi_fd_t sock,
	cloudabi_backlog_t backlog
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_recv(
	cloudabi_fd_t sock,
	const cloudabi_recv_in_t *in,
	cloudabi_recv_out_t *out
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_send(
	cloudabi_fd_t sock,
	const cloudabi_send_in_t *in,
	cloudabi_send_out_t *out
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_shutdown(
	cloudabi_fd_t sock,
	cloudabi_sdflags_t how
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_sock_stat_get(
	cloudabi_fd_t sock,
	cloudabi_sockstat_t *buf,
	cloudabi_ssflags_t flags
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

cloudabi_errno_t
cloudabi_sys_thread_create(
	cloudabi_threadattr_t *attr,
	cloudabi_tid_t *tid
) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}

_Noreturn void
cloudabi_sys_thread_exit(
	_Atomic(cloudabi_lock_t) *lock,
	cloudabi_scope_t scope
) {
	putstring_l(__PRETTY_FUNCTION__);
	while(1) {}
}

cloudabi_errno_t
cloudabi_sys_thread_yield(void) {
	putstring_l(__PRETTY_FUNCTION__);
	return CLOUDABI_ENOSYS;
}
