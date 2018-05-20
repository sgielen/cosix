#include <blockdev/blockdev_store.hpp>
#include <blockdev/blockdev.hpp>
#include <hw/vga_stream.hpp>
#include <oslibc/string.h>
#include <global.hpp>

using namespace cloudos;

blockdev_store::blockdev_store()
: blockdevs_(nullptr)
{}

shared_ptr<blockdev> blockdev_store::get_blockdev(const char *name)
{
	blockdev_list *found = find(blockdevs_, [name](blockdev_list *item) {
		return strcmp(item->data->get_name(), name) == 0;
	});
	return found == nullptr ? shared_ptr<blockdev>() : found->data;
}

cloudabi_errno_t blockdev_store::register_blockdev(shared_ptr<blockdev> i, const char *prefix)
{
	// TODO: do this using printf
	char name[8];
	size_t prefixlen = strlen(prefix);
	if(prefixlen > 6) {
		prefixlen = 6;
	}
	memcpy(name, prefix, prefixlen);

	uint8_t suffix = 0;
	while(suffix < 10) {
		assert(prefixlen + 1 < sizeof(name));
		name[prefixlen] = 0x30 + suffix;
		name[prefixlen + 1] = 0;
		if(get_blockdev(name) == nullptr) {
			return register_blockdev_fixed_name(i, name);
		}
		suffix++;
	}
	return EEXIST;
}

cloudabi_errno_t blockdev_store::register_blockdev_fixed_name(shared_ptr<blockdev> i, const char *name)
{
	if(get_blockdev(name) != nullptr) {
		return EEXIST;
	}

	i->set_name(name);

	blockdev_list *next_entry = allocate<blockdev_list>(i);
	append(&blockdevs_, next_entry);
	return 0;
}
