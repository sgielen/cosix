#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>

int stdout;
std::atomic<bool> b;

void *thread_handler(void *w) {
	bool wait_for = *reinterpret_cast<bool*>(w);
	dprintf(stdout, "Thread started!\n");
	int i = 0;
	while(1) {
		while(b != wait_for) {
			sched_yield();
		}
		if(wait_for) {
			dprintf(stdout, "Ping!\n");
		} else {
			dprintf(stdout, "Pong!\n");
		}
		b = !wait_for;
		if(i++ == 5) {
			pthread_exit(NULL);
		}
	}
}

void program_main(const argdata_t *) {
	stdout = 0;
	dprintf(stdout, "This is thread_test -- Starting threads\n");
	b = true;

	pthread_t thread1, thread2;
	bool _true = true, _false = false;
	pthread_create(&thread1, NULL, thread_handler, &_true);
	pthread_create(&thread2, NULL, thread_handler, &_false);

	// pthread_join() needs poll() support, which is not done yet,
	// so I'll just exit the main thread and when all threads are
	// dead, cloudlibc calls exit(0)
	pthread_exit(NULL);
}
