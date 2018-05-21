#include "hw/multiboot.hpp"
#include "global.hpp"
#include <stdint.h>

using namespace cloudos;

/** Reference for this struct
 * http://git.savannah.gnu.org/cgit/grub.git/tree/doc/multiboot.texi?h=multiboot
 */

extern uint32_t _kernel_virtual_base;

namespace cloudos {
struct boot_info {
	uint32_t flags;

	uint32_t mem_lower;
	uint32_t mem_upper;

	uint32_t boot_device;

	uint32_t cmdline; /* char* */

	uint32_t mods_count;
	uint32_t mods_addr;

	uint32_t syms[4];

	uint32_t mmap_length;
	uint32_t mmap_addr;

	uint32_t drives_length;
	uint32_t drives_addr;

	uint32_t config_table;

	uint32_t boot_loader_name;

	uint32_t apm_table;

	uint32_t vbe_control_info;
	uint32_t vbe_mode_info;
	uint16_t vbe_mode;
	uint16_t vbe_interface_seg;
	uint16_t vbe_interface_off;
	uint16_t vbe_interface_len;

	uint64_t framebuffer_addr;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t framebuffer_bpp;
	uint8_t framebuffer_type;
	uint8_t color_info[6];
} __attribute__((packed));
}

#define FLAG_MEMORY 1
#define FLAG_CMDLINE 4
#define FLAG_MODULE 8
#define FLAG_MMAP 64

multiboot_info::multiboot_info(void *a, uint32_t magic)
: bi(reinterpret_cast<boot_info*>(a))
, valid(magic == MULTIBOOT_MAGIC)
{
}

bool multiboot_info::is_valid() const {
	return valid;
}

char *multiboot_info::cmdline() const {
	if(bi->flags & FLAG_CMDLINE) {
		return reinterpret_cast<char*>(bi->cmdline + _kernel_virtual_base);
	} else {
		return nullptr;
	}
}

bool multiboot_info::mem_amount(uint32_t *mem_lower, uint32_t *mem_upper) const {
	if(bi->flags & FLAG_MEMORY) {
		if(mem_lower) *mem_lower = bi->mem_lower;
		if(mem_upper) *mem_upper = bi->mem_upper;
		return true;
	} else {
		return false;
	}
}

size_t multiboot_info::memory_map(memory_map_entry **first) const {
	if((bi->flags & FLAG_MMAP) == 0 || bi->mmap_length == 0) {
		return 0;
	}

	*first = reinterpret_cast<memory_map_entry*>(bi->mmap_addr + _kernel_virtual_base);
	return bi->mmap_length;
}

multiboot_module *multiboot_info::module_base_address() const {
	if((bi->flags & FLAG_MODULE) == 0 || bi->mods_count == 0) {
		get_vga_stream() << "No module base address\n";
		return nullptr;
	}

	auto *r = reinterpret_cast<multiboot_module*>(bi->mods_addr + _kernel_virtual_base);
	r->mmo_start += _kernel_virtual_base;
	r->mmo_end += _kernel_virtual_base;
	return r;
}
