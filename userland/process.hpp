#pragma once

#include <stdint.h>
#include <stddef.h>

extern "C"
int getpid();

extern "C"
void putstring(const char*, size_t len);

void putstring(const char*);

void process_main();
