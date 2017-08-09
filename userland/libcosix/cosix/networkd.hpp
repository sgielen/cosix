#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace cosix {
namespace networkd {

int open(int rootfd);
int get_socket(int networkd, int type, std::string connect, std::string bind);

}
}
