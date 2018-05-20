#include <blockdev/blockdev.hpp>
#include <blockdev/blockdev_store.hpp>
#include <global.hpp>
#include <oslibc/string.h>

using namespace cloudos;

blockdev::blockdev()
: fd_t(CLOUDABI_FILETYPE_BLOCK_DEVICE, 0, "unnamed blockdev")
{
}

blockdev::~blockdev()
{
}

void blockdev::set_name(const char *n)
{
	memcpy(name, n, sizeof(name));
}

size_t blockdev::pread(void *str, size_t count, size_t offset)
{
	// Convert count to sectorcount
	if(count == 0) {
		error = 0;
		return 0;
	}
	if((count % sector_size) != 0) {
		// count must be a complete number of sectors. We can read more
		// and chop it off, but I'm making that the responsibility of
		// the caller
		error = EINVAL;
		return 0;
	}
	// TODO: count and offset should be uint64_t to access large drives
	uint64_t sectorcount = count / sector_size;

	// Convert offset to LBA
	if((offset % sector_size) != 0) {
		// also here
		error = EINVAL;
		return 0;
	}
	uint64_t lba = offset / sector_size;

	error = read_sectors(str, lba, sectorcount);
	if(error) {
		return 0;
	} else {
		return count;
	}
}


size_t blockdev::pwrite(const char *str, size_t count, size_t offset)
{
	// TODO
	error = ENODEV;
	return 0;
}
