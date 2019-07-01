#include <memory/allocation.hpp>
#include <oslibc/error.h>
#include <oslibc/string.h>
#include <term/terminal.hpp>
#include <term/terminal_fd.hpp>
#include <term/terminal_store.hpp>

using namespace cloudos;

struct termfs_directory_fd : fd_t, enable_shared_from_this<termfs_directory_fd> {
	termfs_directory_fd(const char *n)
	: fd_t(CLOUDABI_FILETYPE_DIRECTORY, 0, n)
	{}

	void lookup(const char *file, size_t filelen, cloudabi_oflags_t, cloudabi_filestat_t *filestat) override
	{
		filestat->st_dev = device;
		filestat->st_nlink = 1;
		filestat->st_atim = 0;
		filestat->st_mtim = 0;
		filestat->st_ctim = 0;
		filestat->st_size = 0;

		if (strncmp(file, ".", filelen) == 0) {
			filestat->st_ino = 0;
			filestat->st_filetype = CLOUDABI_FILETYPE_DIRECTORY;
			error = 0;
			return;
		}

		auto terminals = get_terminal_store()->get_terminals();
		size_t i = 1;
		bool found = false;
		iterate(terminals, [&](terminal_list *item) {
			++i;
			if (strncmp(item->data->get_name(), file, filelen) == 0) {
				filestat->st_ino = i;
				filestat->st_filetype = CLOUDABI_FILETYPE_CHARACTER_DEVICE;
				found = true;
			}
		});
		if (found) {
			error = 0;
		} else {
			error = ENOENT;
		}
	}

	shared_ptr<fd_t> inode_open(cloudabi_device_t st_dev, cloudabi_inode_t st_ino, const cloudabi_fdstat_t *stat) override
	{
		if (st_dev != device) {
			error = EINVAL;
			return nullptr;
		}

		auto terminals = get_terminal_store()->get_terminals();
		size_t i = 1;
		shared_ptr<fd_t> res;
		iterate(terminals, [&](terminal_list *item) {
			if (++i == st_ino) {
				res = make_shared<terminal_fd>(item->data, stat->fs_flags);
			}
		});
		if (res) {
			error = 0;
		} else {
			error = ENOENT;
		}
		return res;
	}

	size_t readdir(char *buf, size_t nbyte, cloudabi_dircookie_t cookie) override {
		auto terminals = get_terminal_store()->get_terminals();
		cloudabi_dircookie_t entrynum = 0;
		size_t written = 0;
		iterate(terminals, [&](terminal_list *item) {
			if(cookie > entrynum++ || nbyte == 0) {
				return;
			}
			cloudabi_dirent_t dirent;
			dirent.d_next = entrynum;
			dirent.d_ino = entrynum;
			const char *name = item->data->get_name();
			dirent.d_namlen = strlen(name);
			dirent.d_type = CLOUDABI_FILETYPE_CHARACTER_DEVICE;

			size_t copy = sizeof(dirent) < nbyte ? sizeof(dirent) : nbyte;
			memcpy(buf, &dirent, copy);
			buf += copy;
			written += copy;
			nbyte -= copy;

			copy = dirent.d_namlen < nbyte ? dirent.d_namlen : nbyte;
			memcpy(buf, name, copy);
			buf += copy;
			written += copy;
			nbyte -= copy;
		});
		error = 0;
		return written;
	}
};

terminal_store::terminal_store()
: terminals_(nullptr)
{}

shared_ptr<terminal> terminal_store::get_terminal(const char *name)
{
	terminal_list *found = find(terminals_, [name](terminal_list *item) {
		return strcmp(item->data->get_name(), name) == 0;
	});
	return found == nullptr ? nullptr : found->data;
}

cloudabi_errno_t terminal_store::register_terminal(shared_ptr<terminal> i)
{
	if(get_terminal(i->get_name()) != nullptr) {
		return EEXIST;
	}

	terminal_list *next_entry = allocate<terminal_list>(i);
	append(&terminals_, next_entry);
	return 0;
}

shared_ptr<fd_t> terminal_store::get_root_fd() {
	return make_shared<termfs_directory_fd>("termfs_root");
}
