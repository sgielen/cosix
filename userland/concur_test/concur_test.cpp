#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <pthread.h>
#include <cloudabi_types.h>

int stdout;
std::atomic<int> ctr(0);
std::mutex mtx;
std::condition_variable cv;

extern thread_local cloudabi_tid_t __pthread_thread_id;

void sleep_second() {
	// we don't have clocks yet, so sleep like this
	for(int i = 0; i < 50000000; ++i) {}
}

void sleep_random() {
	int seconds = arc4random_uniform(6) + 2;
	for(int i = 0; i < seconds; ++i) {
		sleep_second();
	}
}

void thread_counter(int num) {
	// sleep a random amount of seconds before taking the lock
	sleep_random();
	dprintf(stdout, "Thread %u started, counting to %d\n", __pthread_thread_id, num + 1);
	std::unique_lock<std::mutex> lock(mtx);
	while(ctr < num) {
		cv.wait(lock);
	}
	ctr++;
	dprintf(stdout, "Thread %u increased counter to %d, the %s value!\n", __pthread_thread_id, ctr.load(), ctr.load() == num + 1 ? "correct" : "wrong");
	sleep_random();
	cv.notify_all();
}

void program_main(const argdata_t *) {
	stdout = 0;

	std::vector<std::thread> threads;
	int num_threads = 3;
	threads.reserve(num_threads);
	dprintf(stdout, "This is concur_test -- Starting %d threads\n", num_threads);

	for(int i = 0; i < num_threads; ++i) {
		std::thread t([i](){
			thread_counter(i);
		});
		threads.emplace_back(std::move(t));
	}

	for(int i = 0; i < 10; ++i) {
		sleep_second();
	}

	dprintf(stdout, "Joining threads\n");
	for(int i = 0; i < num_threads; ++i) {
		threads[i].join();
	}

	dprintf(stdout, "After all threads are joined, counter is %d, that's the %s value!\n", ctr.load(), ctr.load() == num_threads ? "correct" : "wrong");

	exit(0);
}
