
void operator delete (void*) noexcept {
	// TODO(sjors): implement as soon as we have allocations
	// C++ destructors need this operator to be around, even if
	// it won't be used
}

extern "C"
void __cxa_pure_virtual(void) {
	// TODO(sjors): implement an actual panic
	asm volatile("cli; halted: hlt; jmp halted;");
}
