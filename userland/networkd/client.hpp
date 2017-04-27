namespace networkd {

struct client {
	client(int logfd, int fd);
	~client();

	void run();

private:
	int logfd;
	int fd;
};

}
