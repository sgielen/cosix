#pragma once
#include "net/interface.hpp"

namespace cloudos {

struct loopback_interface : public interface
{
	loopback_interface() : interface(hwtype_t::loopback) {}

	inline virtual cloudabi_errno_t send_frame(uint8_t *frame, size_t length) override {
		// no frame on loopback device
		return received_frame(frame, length);
	}
};

}
