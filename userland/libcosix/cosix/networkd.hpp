#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <flower/protocol/switchboard.ad.h>

namespace cosix {
namespace networkd {

int open(int switchboard);
int open(std::shared_ptr<arpc::FileDescriptor> switchboard);
int open(class flower::protocol::switchboard::Switchboard::Stub *switchboard);
int get_socket(int networkd, int type, std::string connect, std::string bind);

}
}
