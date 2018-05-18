#pragma once
#include <fd/userlandsock.hpp>

namespace cloudos {

struct ifstoresock : public userlandsock {
	ifstoresock(const char *n);

private:
	void handle_command(const char *cmd, const char *arg) override;
};

}
