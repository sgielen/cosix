#include <time/clock_store.hpp>

using namespace cloudos;

clock::~clock() {}

void clock_store::register_clock(cloudabi_clockid_t type, clock *obj) {
	assert(type < NUM_CLOCKS);
	assert(clocks[type] == nullptr);
	clocks[type] = obj;
}

clock *clock_store::get_clock(cloudabi_clockid_t type) {
	if(type >= NUM_CLOCKS) {
		return nullptr;
	}
	return clocks[type];
}
