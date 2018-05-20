#pragma once
#include <fd/userlandsock.hpp>

namespace cloudos {

struct blockdevstoresock : public userlandsock {
	blockdevstoresock(const char *n);

private:
	void handle_command(const char *cmd, const char *arg) override;
};

}
