#pragma once

#include <stddef.h>
#include <stdint.h>

#include <fd/fd.hpp>
#include <oslibc/error.h>

namespace cloudos {

struct blockdev : public fd_t {
	blockdev();
	~blockdev() override;

	/**
	 * This method returns the block devices unique name. This pointer is
	 * owned by this interface, and must not be freed. (If the interface is
	 * still being constructed, this method returns the empty string.)
	 */
	inline const char *get_name() {
		return name;
	}

	size_t pread(void *str, size_t count, size_t offset) final override;
	size_t pwrite(const char *str, size_t count, size_t offset) final override;

	virtual cloudabi_errno_t read_sectors(void *str, uint64_t lba, uint64_t sectorcount) = 0;

private:
	void set_name(const char *name);
	// TODO: make this configurable
	static const int sector_size = 512;

	friend struct blockdev_store;
};

}
