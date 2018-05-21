#include <blockdev/partition.hpp>

using namespace cloudos;

partition::partition(shared_ptr<blockdev> b, uint64_t o, uint64_t c)
: bdev(b)
, lba_offset(o)
, sectorcount(c)
{}

partition::~partition()
{}

cloudabi_errno_t partition::read_sectors(void *str, uint64_t lba, uint64_t sc)
{
	if((lba + sc) > sectorcount) {
		// partition is not this big
		return EINVAL;
	}

	// relative -> absolute addressing
	return bdev->read_sectors(str, lba + lba_offset, sc);
}
