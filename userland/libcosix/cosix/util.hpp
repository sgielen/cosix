#pragma once

#include <uv.h>

namespace cosix {

struct procdesc2 {
	uv_loop_t *loop;
	uv_process_t *handle;

	bool terminated;
	int64_t exit_status;
	int term_signal;
};

procdesc2 *program_spawn2(int binary, argdata_t const *argdata);
void program_kill2(procdesc2 *pd2);
void program_wait2(procdesc2 *pd2, int64_t *exit_status, int *term_signal);

}
