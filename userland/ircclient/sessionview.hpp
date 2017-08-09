#pragma once

#include <cloudabi_types.h>
#include <string>
#include <map>
#include <vector>

#include "terminal.hpp"
#include "session.hpp"

namespace cosix {
namespace irc {

enum class window_activity {
	none = 0,
	event = 1,
	message = 2,
	highlight = 3,
};

struct window {
	std::string name;
	std::string interlocutor;
	window_activity activity;
	std::deque<std::string> last_messages;
};

/* session_view responsibilities:
- receives irc events from the session
  - append them to the window, redraw window if active
  - fix activity, redraw activity line if changed
- receives input from the terminal
  - keeps track of current input line, redraws input line if changed (echoing is off)
  - if \n:
    - reinterpret as command?
    - send to current window: append to window, send to irc_session
*/
struct session_view {
	session_view(cloudabi_fd_t terminal, cloudabi_fd_t ircd, std::string nick, std::string user, std::string realname);
	~session_view();

	void run();
	void redraw();

	bool is_highlight(std::string msg);

	inline irc_session_t *get_irc() {
		return session.get_irc();
	}

private:
	void handle_irc_event(irc_event_t const&);
	void handle_input(std::string);
	void handle_command(std::string cmd, std::string args);

	bool has_window(std::string name);
	window &get_active_window();
	window &add_window(std::string name);
	void add_message(std::string message);
	void add_message_to_window(std::string windowname, std::string message, bool is_event);

	// irc status
	std::string nick;

	// window status
	std::string active_window;
	std::map<std::string, window> windows;

	// terminal status
	std::string input;
	size_t cursor_in_input = 0;

	bool running = false;
	cosix::irc::terminal terminal;
	cosix::irc::session session;
};

}
}
