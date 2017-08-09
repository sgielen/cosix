#pragma once

#include <cloudabi_types.h>
#include <thread>
#include <string>
#include <vector>

typedef struct irc_session_s irc_session_t;

namespace cosix {
namespace irc {

struct irc_event_t {
	std::string event;
	std::string origin;
	std::vector<std::string> params;
};

struct session {
	session(cloudabi_fd_t ircd, std::string nick, std::string user, std::string realname);
	~session();

	void set_event_callback(std::function<void(irc_event_t)> callback);
	void clear_event_callback();

	void start();
	void stop();

	void irc_event(irc_event_t const &event);

	inline irc_session_t *get_irc() {
		return irc;
	}

private:
	irc_session_t *irc = nullptr;
	bool running = false;
	std::thread thr;
	std::function<void(irc_event_t)> callback;
};

}
}
