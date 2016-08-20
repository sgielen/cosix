#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>

void program_main(const argdata_t *) {
	dprintf(0, "This is exec_test -- execution succeeded!\n");
	// TODO: print argdata_t contents
	exit(0);
}
