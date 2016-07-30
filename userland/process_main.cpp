extern "C"
int getpid();

extern "C"
void putstring(const char*, unsigned int len);

int pid;

extern "C"
void _start() {
	putstring("Hello ", 6);
	putstring("world!\n", 7);
	pid = getpid();
	if(pid == 0) {
		putstring("Process 0\n", 10);
	} else if(pid == 1) {
		putstring("Process 1\n", 10);
	} else if(pid == 2) {
		putstring("Process 2\n", 10);
	} else if(pid == 3) {
		putstring("Process 3\n", 10);
	} else if(pid == 4) {
		putstring("Process 4\n", 10);
	} else if(pid == 5) {
		putstring("Process 5\n", 10);
	} else if(pid == 6) {
		putstring("Process 6\n", 10);
	} else {
		putstring("High process\n", 13);
	}
	while(1) {}
}
