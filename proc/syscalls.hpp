#pragma once
#include <proc/syscall_context.hpp>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

cloudabi_errno_t syscall_clock_res_get(syscall_context &c);
cloudabi_errno_t syscall_clock_time_get(syscall_context &c);
cloudabi_errno_t syscall_condvar_signal(syscall_context &c);
cloudabi_errno_t syscall_fd_close(syscall_context &c);
cloudabi_errno_t syscall_fd_create1(syscall_context &c);
cloudabi_errno_t syscall_fd_create2(syscall_context &c);
cloudabi_errno_t syscall_fd_datasync(syscall_context &c);
cloudabi_errno_t syscall_fd_dup(syscall_context &c);
cloudabi_errno_t syscall_fd_pread(syscall_context &c);
cloudabi_errno_t syscall_fd_pwrite(syscall_context &c);
cloudabi_errno_t syscall_fd_read(syscall_context &c);
cloudabi_errno_t syscall_fd_replace(syscall_context &c);
cloudabi_errno_t syscall_fd_seek(syscall_context &c);
cloudabi_errno_t syscall_fd_stat_get(syscall_context &c);
cloudabi_errno_t syscall_fd_stat_put(syscall_context &c);
cloudabi_errno_t syscall_fd_sync(syscall_context &c);
cloudabi_errno_t syscall_fd_write(syscall_context &c);
cloudabi_errno_t syscall_file_advise(syscall_context &c);
cloudabi_errno_t syscall_file_allocate(syscall_context &c);
cloudabi_errno_t syscall_file_create(syscall_context &c);
cloudabi_errno_t syscall_file_link(syscall_context &c);
cloudabi_errno_t syscall_file_open(syscall_context &c);
cloudabi_errno_t syscall_file_readdir(syscall_context &c);
cloudabi_errno_t syscall_file_readlink(syscall_context &c);
cloudabi_errno_t syscall_file_rename(syscall_context &c);
cloudabi_errno_t syscall_file_stat_fget(syscall_context &c);
cloudabi_errno_t syscall_file_stat_fput(syscall_context &c);
cloudabi_errno_t syscall_file_stat_get(syscall_context &c);
cloudabi_errno_t syscall_file_stat_put(syscall_context &c);
cloudabi_errno_t syscall_file_symlink(syscall_context &c);
cloudabi_errno_t syscall_file_unlink(syscall_context &c);
cloudabi_errno_t syscall_lock_unlock(syscall_context &c);
cloudabi_errno_t syscall_mem_advise(syscall_context &c);
cloudabi_errno_t syscall_mem_lock(syscall_context &c);
cloudabi_errno_t syscall_mem_map(syscall_context &c);
cloudabi_errno_t syscall_mem_protect(syscall_context &c);
cloudabi_errno_t syscall_mem_sync(syscall_context &c);
cloudabi_errno_t syscall_mem_unlock(syscall_context &c);
cloudabi_errno_t syscall_mem_unmap(syscall_context &c);
cloudabi_errno_t syscall_poll(syscall_context &c);
cloudabi_errno_t syscall_poll_fd(syscall_context &c);
cloudabi_errno_t syscall_proc_exec(syscall_context &c);
cloudabi_errno_t syscall_proc_exit(syscall_context &c);
cloudabi_errno_t syscall_proc_fork(syscall_context &c);
cloudabi_errno_t syscall_proc_raise(syscall_context &c);
cloudabi_errno_t syscall_random_get(syscall_context &c);
cloudabi_errno_t syscall_sock_recv(syscall_context &c);
cloudabi_errno_t syscall_sock_send(syscall_context &c);
cloudabi_errno_t syscall_sock_shutdown(syscall_context &c);
cloudabi_errno_t syscall_thread_create(syscall_context &c);
cloudabi_errno_t syscall_thread_exit(syscall_context &c);
cloudabi_errno_t syscall_thread_yield(syscall_context &c);

}
