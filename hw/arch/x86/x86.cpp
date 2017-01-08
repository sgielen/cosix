#include "x86.hpp"
#include "x86_pit.hpp"
#include "x86_kbd.hpp"
#include <memory/allocation.hpp>

using namespace cloudos;

const char *x86_driver::description() {
	return "x86 PC driver";
}

device *x86_driver::probe_root_device(device *root) {
	return allocate<x86_pc>(root);
}

x86_pc::x86_pc(device *parent) : device(parent) {}

const char *x86_pc::description() {
	return "x86 PC";
}

cloudabi_errno_t x86_pc::init() {
	auto *pit = allocate<x86_pit>(this);
	pit->init();
	auto *kbd = allocate<x86_kbd>(this);
	kbd->init();
	return 0;
}

