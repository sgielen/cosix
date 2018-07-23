#pragma once
#include <blockdev/blockdev.hpp>

namespace cloudos {

struct partition : public blockdev {
	partition(shared_ptr<blockdev> bd, uint64_t lba_offset, uint64_t sectorcount);
	~partition() override;

	cloudabi_errno_t read_sectors(void *str, uint64_t lba, uint64_t sectorcount) override;
	cloudabi_errno_t write_sectors(const void *str, uint64_t lba, uint64_t sectorcount) override;

private:
	shared_ptr<blockdev> bdev;
	uint64_t lba_offset;
	uint64_t sectorcount;
};

}
