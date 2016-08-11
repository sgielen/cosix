#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cloudos {

static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) );
	return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint16_t inw(uint16_t port) {
	uint16_t ret;
	asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) );
	return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
	asm volatile("outw %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint32_t inl(uint16_t port) {
	uint32_t ret;
	asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) );
	return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
	asm volatile("outl %0, %1" : : "a"(val), "Nd"(port) );
}

static inline void cpuid(int page, uint32_t result[4]) {
	asm volatile("cpuid" : "=a"(result[0]), "=b"(result[1]),
		"=c"(result[2]), "=d"(result[3]) : "a"(page));
}

static inline void get_cpu_name(char cpuname[13]) {
	uint32_t result[4];
	cpuid(0, result);
	cpuname[ 0] = result[1]       & 0xff;
	cpuname[ 1] = result[1] >> 8  & 0xff;
	cpuname[ 2] = result[1] >> 16 & 0xff;
	cpuname[ 3] = result[1] >> 24       ;
	cpuname[ 4] = result[3]       & 0xff;
	cpuname[ 5] = result[3] >> 8  & 0xff;
	cpuname[ 6] = result[3] >> 16 & 0xff;
	cpuname[ 7] = result[3] >> 24       ;
	cpuname[ 8] = result[2]       & 0xff;
	cpuname[ 9] = result[2] >> 8  & 0xff;
	cpuname[10] = result[2] >> 16 & 0xff;
	cpuname[11] = result[2] >> 24       ;
	cpuname[12] = 0;
}

}
