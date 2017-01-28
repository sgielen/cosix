#pragma once
#include <proc/syscall_context.hpp>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

cloudabi_errno_t syscall_clock_res_get(syscall_context &c);
cloudabi_errno_t syscall_clock_time_get(syscall_context &c);

}
