#include "../../term/escape_codes.hpp"
#include "sessionview.hpp"
#include <cassert>
#include <libircclient.h>
#include <sstream>
#include <tuple>

using namespace cloudos;
using namespace cosix::irc;

struct origin_t {
	std::string nick;
	std::string user;
	std::string host;
};

static origin_t parse_origin(std::string origin) {
	origin_t o;
	o.nick.reserve(origin.size());
	o.user.reserve(origin.size());
	o.host.reserve(origin.size());
	int phase = 0;
	for(size_t i = 0; i < origin.size(); ++i) {
		char c = origin[i];
		switch(phase) {
		case 0:
			if(c == '!') {
				phase = 1;
			} else {
				o.nick += c;
			}
			break;
		case 1:
			if(c == '@') {
				phase = 2;
			} else {
				o.user += c;
			}
			break;
		case 2:
			o.host += c;
		}
	}
	return o;
}

static std::string tolower(std::string s) {
	for(auto &c : s) {
		c = tolower(c);
	}
	return s;
}

bool is_equal_ci(std::string const &a, std::string const &b) {
	return tolower(a) == tolower(b);
}

session_view::session_view(cloudabi_fd_t t, cloudabi_fd_t i, std::string n, std::string user, std::string realname)
: nick(n)
, terminal(t)
, session(i, nick, user, realname)
{
	session.set_event_callback([this](irc_event_t event) {
		// TODO: this callback is called from another thread; ideally,
		// we'd transfer this event back to the main thread before calling
		// this function:
		// move it into a queue, then bump a CV; in the thread waiting on the
		// terminal, wait on the CV as well
		// -- or, alternatively, put the terminal into a thread as well and let
		// its events trigger the same CV
		handle_irc_event(event);
	});

	add_window("server").interlocutor.clear();
	active_window = "server";
}

session_view::~session_view()
{
	if(running) {
		fprintf(stderr, "Session view destruct while still running\n");
		abort();
	}
}

bool session_view::is_highlight(std::string message)
{
	return tolower(message).find(tolower(nick)) != std::string::npos;
}

static bool is_numeric(std::string event)
{
	std::stringstream ss;
	ss << event;
	unsigned int i;
	ss >> i;
	return std::to_string(i) == event;
}

void session_view::handle_irc_event(irc_event_t const &event)
{
	auto origin = parse_origin(event.origin);
	if(event.event == "CHANNEL") {
		if(has_window(event.params[0])) {
			add_message_to_window(event.params[0], "<" + origin.nick + "> " + event.params[1], false);
		} else {
			add_message_to_window("server", "<" + event.params[0] + " " + origin.nick + "> " + event.params[1], false);
		}
	} else if(event.event == "JOIN") {
		if(is_equal_ci(origin.nick, nick) && !has_window(event.params[0])) {
			add_window(event.params[0]);
			active_window = event.params[0];
		}
		add_message_to_window(event.params[0], "-!- " + origin.nick + "(" + origin.user + "@" + origin.host + ") joined", true);
	} else if(event.event == "PART") {
		if(is_equal_ci(origin.nick, nick) && has_window(event.params[0])) {
			// TODO close window?
		}
		add_message_to_window(event.params[0], "-!- " + origin.nick + "(" + origin.user + "@" + origin.host + ") parted", true);
	} else if(event.event == "NICK") {
		// TODO: write a message about this in all channels the nick is in
		if(is_equal_ci(origin.nick, nick)) {
			// my nick changed
			nick = origin.nick;
		}
	} else if(is_numeric(event.event)) {
		if(event.event == "372" || event.event == "376") {
			// ignore
		} else {
			std::string args;
			for(size_t i = 0; i < event.params.size(); ++i) {
				if(i == event.params.size() - 1) {
					args += ":" + event.params[i];
				} else {
					args += event.params[i] + " ";
				}
			}
			add_message("-!- " + event.origin + " " + event.event + " " + args);
		}
	} else {
		add_message("-!- " + event.event);
	}
}

void session_view::redraw()
{
	auto &window = get_active_window();

	auto term_size = terminal.size();
	if(term_size.second < 2) {
		return;
	}

	size_t messages_lines = term_size.second - 2;
	size_t lines_empty_begin = 0;
	size_t lines_taken = 0;
	size_t messages_fit = 0;

	auto it = window.last_messages.crbegin();
	while(lines_taken < messages_lines && it != window.last_messages.crend()) {
		size_t lines_remaining = messages_lines - lines_taken;
		auto msgsize = terminal.predict_size_of_input(*it);
		if(msgsize.second <= lines_remaining) {
			// this fits completely
			messages_fit++;
			lines_taken += msgsize.second;
		} else {
			// doesn't fit completely, so leave these lines empty
			// TODO: print line partially
			lines_empty_begin = lines_remaining;
			lines_taken += lines_remaining;
		}
		it++;
	}

	// if lines_empty_begin > 0, leave empty lines at the top
	// if lines_taken < messages_lines, leave empty lines at the end

	// remove all lines that don't fit anymore anyway
	{
		std::deque<std::string> messages;
		it = window.last_messages.crbegin();
		for(size_t i = 0; i < messages_fit; ++i) {
			assert(it != window.last_messages.crend());
			messages.push_front(*it);
			it++;
		}
		window.last_messages = messages;
	}

	// this newline is unnecessary but removes a previously written partial
	// line if the terminal doesn't implement screen clearing
	terminal.write("\n");

	terminal.disable_cursor_display();
	terminal.clear_screen();

	std::string buf;

	while(lines_empty_begin > 0) {
		buf.append("\n");
		lines_empty_begin--;
	}

	for(auto const &msg : window.last_messages) {
		buf.append(msg + "\n");
	}

	while(lines_taken <= messages_lines) {
		buf.append("\n");
		lines_taken++;
	}

	// TODO: write the activity line
	buf.append("\x1b[44m[no activity]\x1b[49m\n");

	// write the input line
	std::string preinput = "[" + active_window + "] ";
	buf.append(preinput);
	buf.append(input);
	terminal.write(buf);

	// TODO: if preinput contains escape sequences, don't use .size(), but
	// use predict_size_of_input
	terminal.set_cursor(term_size.second - 2, preinput.size() + cursor_in_input);
	terminal.enable_cursor_display();
}

window &session_view::get_active_window()
{
	auto it = windows.find(tolower(active_window));
	if(it == windows.end()) {
		abort();
	}
	return it->second;
}

bool session_view::has_window(std::string name)
{
	auto it = windows.find(tolower(name));
	return it != windows.end();
}

window &session_view::add_window(std::string name)
{
	auto it = windows.find(tolower(name));
	if(it != windows.end()) {
		add_message("-!- DEBUG: trying to add window that already exists: '" + name+ "'");
		return it->second;
	}
	window w{name, name, window_activity::none, {}};
	return windows.emplace(tolower(name), std::move(w)).first->second;
}

void session_view::handle_command(std::string cmd, std::string args)
{
	if(cmd.empty()) {
		auto &win = get_active_window();
		if(win.interlocutor.empty()) {
			add_message(">>> " + args);
			irc_send_raw(get_irc(), "%s", args.c_str());
		} else {
			add_message_to_window(active_window, "<" + nick + "> " + args, false);
			irc_cmd_msg(get_irc(), win.interlocutor.c_str(), args.c_str());
		}
	} else if(cmd == "quit") {
		running = false;
	} else if(cmd == "help") {
		add_message("-!- Help is unimplemented, TODO.");
	} else if(cmd == "join") {
		if(args.empty()) {
			add_message("-!- Usage: /join <channel>");
			return;
		}
		irc_cmd_join(get_irc(), args.c_str(), "");
	} else if(cmd == "part") {
		// TODO win.interlocutor if no args
		irc_cmd_part(get_irc(), args.c_str());
	} else if(cmd == "w" || cmd == "win" || cmd == "window") {
		if(!has_window(args)) {
			add_message("-!- No such window: " + args);
		} else {
			active_window = args;
			redraw();
		}
	} else {
		add_message("-!- Unknown command: " + cmd);
	}
}

void session_view::add_message(std::string msg)
{
	add_message_to_window(active_window, msg, true);
}

void session_view::add_message_to_window(std::string window, std::string msg, bool is_event) {
	auto it = windows.find(tolower(window));
	if(it == windows.end()) {
		if(active_window == window) {
			abort();
		}
		add_message("-!- DEBUG: Message added to nonexistant window '" + window + "'");
		return;
	}

	// TODO: prepend time to message

	assert(is_equal_ci(it->second.name, window));
	window_activity set_activity;
	if(is_event) {
		set_activity = window_activity::event;
	} else if(!is_event && is_highlight(msg)) {
		// TODO: actually mark the highlight in the msg here
		set_activity = window_activity::highlight;
	} else {
		set_activity = window_activity::message;
	}
	it->second.last_messages.push_back(msg);

	if(active_window == window) {
		redraw();
	} else if(set_activity > it->second.activity) {
		it->second.activity = set_activity;
		// TODO: redraw only the activity line
		redraw();
	}
}

void session_view::handle_input(std::string s)
{
	if(s.empty()) {
		return;
	}

	if(s[0] == '/') {
		std::string cmd;
		std::string arg;
		int stage = 0;
		for(size_t i = 1; i < s.length(); ++i) {
			switch(stage) {
			case 0: // reading spaces before cmd
				if(!isspace(s[i])) {
					cmd += s[i];
					stage = 1;
				}
				break;
			case 1: // reading cmd
				if(isspace(s[i])) {
					stage = 2;
				} else {
					cmd += s[i];
				}
				break;
			case 2: // reading spaces before arg
				if(!isspace(s[i])) {
					arg += s[i];
					stage = 3;
				}
				break;
			case 3: // reading arg, regardless of spaces
				arg += s[i];
			}
		}
		if(cmd.empty()) {
			// nothing given at all
		} else if(cmd[0] == '/') {
			// this was an escape, remove first '/' and carry on as regular input
			handle_command("", s.substr(1));
		} else {
			handle_command(cmd, arg);
		}
		return;
	}

	// it's just regular input
	handle_command("", s);
}

void session_view::run()
{
	terminal.disable_echoing();
	terminal.request_size();
	terminal.wait_for_size();
	redraw();
	running = true;
	cursor_in_input = 0;
	input.clear();
	add_message("-!- Starting IRC connection...");
	redraw();
	session.start();
	while(running) {
		assert(cursor_in_input <= input.size());

		// blocks for next token
		// TODO: interrupt block when IRC session thread has an event?
		std::string token = terminal.get_token();
		if(token == "\n") {
			std::string i = input;
			input.clear();
			cursor_in_input = 0;
			handle_input(i);
			redraw();
		} else if(token == "\b") {
			// TODO: if cursor is not at end of input, this will behave incorrectly
			if(cursor_in_input > 0) {
				input = input.substr(0, input.size() - 1);
				cursor_in_input--;
				redraw();
			}
		} else if(is_control_character(token.c_str(), token.size())) {
			add_message("-!- Read control character from terminal.");
		} else if(is_escape_sequence(token.c_str(), token.size())) {
			// TODO: handle left, right, tab
			//add_message("-!- Read escape sequence of size " + std::to_string(token.size()) + " from terminal: "\"^[" + token.substr(1) + "\"");
		} else {
			// normal text
			input.append(token);
			cursor_in_input += token.size();
			// echo, TODO: rewrite the input line if cursor isn't at the end of the input
			terminal.write(token);
		}
	}
	add_message("-!- Stopping IRC connection...");
	session.clear_event_callback();
	session.stop();
	terminal.enable_echoing();
}
