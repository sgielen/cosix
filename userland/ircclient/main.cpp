#include <argdata.h>
#include <program.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sessionview.hpp"

void program_main(const argdata_t *ad) {
	bool have_terminal = false;
	bool have_ircd = false;
	bool have_nick = false;
	bool have_user = false;
	bool have_realname = false;
	int terminal = 0, ircd = 0;
	std::string nick, user, realname;

	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_get(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			argdata_map_next(&it);
			continue;
		}

		const char *cstr = nullptr;
		size_t len = 0;

		if(strcmp(keystr, "terminal") == 0) {
			have_terminal = true;
			argdata_get_fd(value, &terminal);
		} else if(strcmp(keystr, "ircd") == 0) {
			have_ircd = true;
			argdata_get_fd(value, &ircd);
		} else if(strcmp(keystr, "nick") == 0) {
			have_nick = true;
			argdata_get_str(value, &cstr, &len);
			nick.assign(cstr, len);
		} else if(strcmp(keystr, "user") == 0) {
			have_user = true;
			argdata_get_str(value, &cstr, &len);
			user.assign(cstr, len);
		} else if(strcmp(keystr, "realname") == 0) {
			have_realname = true;
			argdata_get_str(value, &cstr, &len);
			realname.assign(cstr, len);
		}
		argdata_map_next(&it);
	}

	if(!have_terminal) {
		exit(1);
	}
	if(!have_ircd || !have_nick) {
		dprintf(terminal, "Missing required parameters\n");
		exit(1);
	}
	if(!have_user) {
		user = nick;
	}
	if(!have_realname) {
		realname = nick;
	}

	dprintf(terminal, "IRC client starting...\n");
	FILE *out = fdopen(terminal, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);
	fprintf(stderr, "stderr is working to terminal.\n");

	cosix::irc::session_view sv(terminal, ircd, nick, user, realname);
	sv.run();
	close(terminal);
	close(ircd);
	exit(0);
}
