#include "sse.hpp"
#include "cpu_io.hpp"
#include "global.hpp"

using namespace cloudos;

#ifndef TESTING_ENABLED
#define CPUID_REG_EDX 3
#define CPUID_FEAT_EDX_SSE 1 << 25
#endif

void cloudos::sse_enable() {
#ifndef TESTING_ENABLED
	uint32_t features[4];
	cpuid(1, features);
	if(!(features[CPUID_REG_EDX] & CPUID_FEAT_EDX_SSE)) {
		kernel_panic("SSE is not supported on this CPU. CloudABI binaries won't run.");
	}

	// Turn on the necessary cr0 and cr4 bits
	asm volatile(
		"mov %%cr0, %%eax\n"  // Read cr0
		"and $0xfffb, %%ax\n" // Clear x87 emulation CR0.EM
		"or $0x2, %%ax\n"     // Set x87 monitoring CR0.MP
		"mov %%eax, %%cr0\n"  // Write cr0
		"mov %%cr4, %%eax\n"  // Read cr4
		"or $0x200, %%ax\n"     // Enable SSE CR4.OSFXSR
		"or $0x400, %%ax\n"     // Enable SSE exceptions CR4.OSXMMEXCPT
		"mov %%eax, %%cr4\n"
		: : : "eax");
#endif
}
