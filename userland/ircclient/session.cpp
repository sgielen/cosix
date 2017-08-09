#include "session.hpp"

#include <libircclient.h>
#include <string.h>

#include <cassert>
#include <string>
#include <vector>

using namespace cosix::irc;

static session &get_session(irc_session_t *irc) {
	return *reinterpret_cast<session*>(irc_get_ctx(irc));
}

static void irc_event_callback(irc_session_t *irc, const char *e, const char *o, const char **p, unsigned int count) {
	irc_event_t event;
	if(e) event.event = e;
	if(o) event.origin = o;
	event.params.resize(count);
	for(size_t i = 0; i < count; ++i) {
		event.params[i] = p[i];
	}
	get_session(irc).irc_event(event);
}

static void irc_eventcode_callback(irc_session_t *irc, unsigned int e, const char *o, const char **p, unsigned int count) {
	std::string event = std::to_string(e);
	irc_event_callback(irc, event.c_str(), o, p, count);
}

session::session(cloudabi_fd_t ircd, std::string nick, std::string user, std::string realname)
{
	clear_event_callback();
	irc_callbacks_t callbacks;
	memset(&callbacks, sizeof(irc_callbacks_t), 0);
	callbacks.event_connect = irc_event_callback;
	callbacks.event_nick = irc_event_callback;
	callbacks.event_quit = irc_event_callback;
	callbacks.event_join = irc_event_callback;
	callbacks.event_part = irc_event_callback;
	callbacks.event_mode = irc_event_callback;
	callbacks.event_umode = irc_event_callback;
	callbacks.event_topic = irc_event_callback;
	callbacks.event_kick = irc_event_callback;
	callbacks.event_channel = irc_event_callback;
	callbacks.event_privmsg = irc_event_callback;
	callbacks.event_notice = irc_event_callback;
	callbacks.event_channel_notice = irc_event_callback;
	callbacks.event_invite = irc_event_callback;
	callbacks.event_ctcp_req = irc_event_callback;
	callbacks.event_ctcp_rep = irc_event_callback;
	callbacks.event_ctcp_action = irc_event_callback;
	callbacks.event_unknown = irc_event_callback;
	callbacks.event_numeric = irc_eventcode_callback;
	irc = irc_create_session(&callbacks);
	//irc_option_set(irc, LIBIRC_OPTION_DEBUG);
	irc_set_ctx(irc, this);
	assert(&get_session(irc) == this);

	if(irc == nullptr) {
		fprintf(stderr, "Failed to create IRC session\n");
		abort();
	}

	if(irc_set_sockfd(irc, ircd, 0 /* SSL? */, "" /* pass */, nick.c_str(), user.c_str(), realname.c_str()) != 0) {
		fprintf(stderr, "Failed to initialize IRC session: %d - %s\n",
			irc_errno(irc), irc_strerror(irc_errno(irc)));
		abort();
	}
}

session::~session()
{
	if(thr.joinable()) {
		fprintf(stderr, "Session destruct while thread is still running\n");
		abort();
	}
	irc_destroy_session(irc);
}

void session::set_event_callback(std::function<void(irc_event_t)> c)
{
	callback = c;
}

void session::clear_event_callback()
{
	callback = [](irc_event_t){};
}

void session::irc_event(irc_event_t const &event)
{
	callback(event);
}

void session::start()
{
	running = true;
	std::thread t([this](){
		while(irc_is_connected(irc)) {
			struct timeval tv;
			fd_set in_set, out_set;
			int maxfd = 0;

			tv.tv_usec = 250000;
			tv.tv_sec = 0;

			FD_ZERO (&in_set);
			FD_ZERO (&out_set);

			irc_add_select_descriptors (irc, &in_set, &out_set, &maxfd);

			if(select(maxfd + 1, &in_set, &out_set, 0, &tv) < 0) {
				fprintf(stderr, "select() failed: %d - %s\n", errno, strerror(errno));
				running = false;
				break;
			}

			if(irc_process_select_descriptors(irc, &in_set, &out_set) && running) {
				fprintf(stderr, "process_select_descriptors failed: %d - %s\n", irc_errno(irc), irc_strerror(irc_errno(irc)));
				running = false;
				break;
			}
		}
		if(running) {
			fprintf(stderr, "IRC disconnected unexpectedly, stopping...\n");
			running = false;
		}
	});
	std::swap(thr, t);
}

void session::stop()
{
	running = false;
	irc_cmd_quit(irc, "Client exiting");
	thr.join();
}
