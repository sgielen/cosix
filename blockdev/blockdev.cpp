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

bool blockdev::convert_count_offset(uint64_t &count, uint64_t &offset) {
	// Convert count to sectorcount
	if(count == 0) {
		error = 0;
		return false;
	}
	if((count % sector_size) != 0) {
		// count must be a complete number of sectors. We can read more
		// and chop it off, but I'm making that the responsibility of
		// the caller
		error = EINVAL;
		return false;
	}
	count /= sector_size;

	// Convert offset to LBA
	if((offset % sector_size) != 0) {
		// also here
		error = EINVAL;
		return false;
	}
	offset /= sector_size;
	return true;
}

size_t blockdev::pread(void *str, size_t count, size_t offset)
{
	// TODO: count and offset should be uint64_t to access large drives
	uint64_t sectorcount = count;
	uint64_t lba = offset;
	if(!convert_count_offset(sectorcount, lba)) {
		return 0;
	}

	error = read_sectors(str, lba, sectorcount);
	if(error) {
		return 0;
	} else {
		return count;
	}
}

size_t blockdev::pwrite(const char *str, size_t count, size_t offset)
{
	// TODO: count and offset should be uint64_t to access large drives
	uint64_t sectorcount = count;
	uint64_t lba = offset;
	if(!convert_count_offset(sectorcount, lba)) {
		return 0;
	}

	error = write_sectors(str, lba, sectorcount);
	if(error) {
		return 0;
	} else {
		return count;
	}
}
