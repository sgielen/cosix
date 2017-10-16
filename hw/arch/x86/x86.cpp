#include "x86.hpp"
#include "x86_ata.hpp"
#include "x86_pit.hpp"
#include "x86_kbd.hpp"
#include "x86_serial.hpp"
#include "x86_fpu.hpp"
#include "x86_rtc.hpp"
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
	auto *serial = allocate<x86_serial>(this);
	serial->init();
	auto *fpu = allocate<x86_fpu>(this);
	fpu->init();
	auto *rtc = allocate<x86_rtc>(this);
	rtc->init();
	auto *ata = allocate<x86_ata>(this);
	ata->init();
	return 0;
}

