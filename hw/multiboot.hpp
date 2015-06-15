#pragma once
#include <stdint.h>

namespace cloudos {

#define MULTIBOOT_MAGIC 0x2BADB002

struct boot_info;

struct multiboot_info {
	multiboot_info(void *addr, uint32_t magic = MULTIBOOT_MAGIC);

	bool is_valid() const;
	bool mem_amount(uint32_t *mem_lower, uint32_t *mem_upper) const;

private:
	boot_info *bi;
	bool valid;
};

}
