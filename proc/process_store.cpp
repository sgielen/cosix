#include <proc/process_store.hpp>
#include <memory/allocation.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

process_store::process_store()
: processes_(nullptr)
{}

void process_store::register_process(shared_ptr<process_fd> f) {
#ifndef NDEBUG
	for (weak_ptr<process_fd> weak : processes_) {
		shared_ptr<process_fd> fd = weak.lock();
		if (fd && fd == f) {
			kernel_panic("process registering to process store is already registered");
		}
	}
#endif

	process_weaklist *new_entry = allocate<process_weaklist>(f);
	append(&processes_, new_entry);
}

shared_ptr<process_fd> process_store::find_process(uint8_t const *pid) {
	uint8_t p[16];
	// TODO: O(n) -> O(log n)
	for(weak_ptr<process_fd> weakfd : processes_) {
		shared_ptr<process_fd> fd = weakfd.lock();
		if (!fd) {
			// TODO: process already exited, should take entry out of processes_
			continue;
		}
		if (memcmp(p, pid, sizeof(p)) == 0) {
			return fd;
		}
	}
	return nullptr;
}
