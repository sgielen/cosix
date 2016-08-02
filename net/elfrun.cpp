#include "net/elfrun.hpp"
#include "global.hpp"
#include "hw/vga_stream.hpp"
#include "oslibc/string.h"
#include "memory/allocator.hpp"
#include "fd/process_fd.hpp"
#include "fd/scheduler.hpp"

using namespace cloudos;

elfrun_implementation::elfrun_implementation()
: pos(0)
, awaiting(false)
{
}

template <typename T>
T elf_endian(T value, uint8_t elf_data) {
	// TODO: know whether we are big or little endian
	if(elf_data == 1) return value;
	T r;
	uint8_t *net = reinterpret_cast<uint8_t*>(&value);
	uint8_t *res = reinterpret_cast<uint8_t*>(&r);
	for(size_t i = 0; i < sizeof(T); ++i) {
		res[i] = net[sizeof(T)-i-1];
	}
	return r;
}

error_t elfrun_implementation::run_binary() {
	const char *elf_magic = "\x7F" "ELF";
	if(memcmp(buffer, elf_magic, 4) != 0) {
		get_vga_stream() << "  Not an ELF binary\n";
		return error_t::invalid_argument;
	}

	uint8_t elf_class = buffer[0x4];
	if(elf_class != 1 /* 32 bit */) {
		get_vga_stream() << "  Invalid ELF class: " << elf_class << "\n";
	}

	uint8_t elf_data = buffer[0x5];
	// 1 is little endian, 2 is big endian
	if(elf_data != 1 && elf_data != 2) {
		get_vga_stream() << "  Invalid ELF data class: " << elf_data << "\n";
	}

	uint8_t elf_version = buffer[0x6];
	if(elf_version != 1) {
		get_vga_stream() << "  Invalid ELF version: " << elf_version << "\n";
	}

	uint8_t elf_osabi = buffer[0x7];
	uint8_t elf_abiversion = buffer[0x8];
	get_vga_stream() << "  ELF OS ABI " << uint16_t(elf_osabi) << ", version " << uint16_t(elf_abiversion) << "\n";

	uint16_t elf_type = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x10]), elf_data);
	if(elf_type != 2 /* executable */) {
		get_vga_stream() << "  Invalid ELF type: " << elf_type << "\n";
	}

	uint16_t elf_machine = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x12]), elf_data);
	if(elf_machine != 0x03 /* x86 */) {
		get_vga_stream() << "  Invalid ELF machine: 0x" << hex << elf_machine << dec << "\n";
	}

	uint32_t elf_version2 = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[0x14]), elf_data);
	if(elf_version2 != 1) {
		get_vga_stream() << "  Invalid ELF version 2: " << elf_version2 << "\n";
	}

	// from here, the length depends on whether this ELF is 32-bits or 64-bits
	uint32_t elf_entry = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[0x18]), elf_data);
	get_vga_stream() << "  ELF entry point: 0x" << hex << elf_entry << dec << "\n";

	uint32_t elf_phoff = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[0x1c]), elf_data);
	uint32_t elf_shoff = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[0x20]), elf_data);
	uint32_t elf_flags = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[0x24]), elf_data);
	uint16_t elf_ehsize = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x28]), elf_data);
	uint16_t elf_phentsize = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x2a]), elf_data);
	uint16_t elf_phnum = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x2c]), elf_data);
	uint16_t elf_shentsize = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x2e]), elf_data);
	uint16_t elf_shnum = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x30]), elf_data);
	uint16_t elf_shstrndx = elf_endian(*reinterpret_cast<uint16_t*>(&buffer[0x32]), elf_data);

	uint32_t elf_shstr_off = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[elf_shoff + elf_shentsize * elf_shstrndx + 0x10]), elf_data);

	process_fd *process = get_allocator()->allocate<process_fd>();
	new(process) process_fd(get_page_allocator(), "elfrun process");

	// read the program headers
	for(uint16_t phnum = 0; phnum < elf_phnum; ++phnum) {
		uint16_t offset = elf_phoff + elf_phentsize * phnum;
		get_vga_stream() << "  ELF program header " << phnum << "\n";
		uint32_t ph_type = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x00]), elf_data);
		get_vga_stream() << "    type: 0x" << hex << ph_type << dec << "\n";
		uint32_t ph_offset = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x04]), elf_data);
		get_vga_stream() << "    offset of segment: 0x" << hex << ph_offset << dec << "\n";
		uint32_t ph_vaddr = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x08]), elf_data);
		get_vga_stream() << "    virtual address: 0x" << hex << ph_vaddr << dec << "\n";
		uint32_t ph_paddr = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x0c]), elf_data);
		get_vga_stream() << "    physical address: 0x" << hex << ph_paddr << dec << "\n";
		uint32_t ph_filesz = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x10]), elf_data);
		get_vga_stream() << "    size in file image: 0x" << hex << ph_filesz << dec << "\n";
		uint32_t ph_memsz = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x14]), elf_data);
		get_vga_stream() << "    size in memory: 0x" << hex << ph_memsz << dec << "\n";
		uint32_t ph_flags = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x18]), elf_data);
		get_vga_stream() << "    segment flags: 0x" << hex << ph_flags << dec << "\n";
		uint32_t ph_align = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x1c]), elf_data);
		get_vga_stream() << "    alignment: 0x" << hex << ph_align << dec << "\n";

		if(ph_type == 1 /* PT_LOAD */) {
			uint8_t *codebuf = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(ph_memsz, 4096));
			memcpy(codebuf, buffer + ph_offset, ph_filesz);
			for(uint32_t nullsz = ph_filesz; nullsz < ph_memsz; ++nullsz) {
				codebuf[nullsz] = 0;
			}

			process->map_at(codebuf, reinterpret_cast<void*>(ph_vaddr), ph_memsz);
		}
	}

	process->initialize(reinterpret_cast<void*>(elf_entry), get_allocator());
	get_scheduler()->process_fd_ready(process);

	/*for(uint16_t shnum = 0; shnum < elf_shnum; ++shnum) {
		uint16_t offset = elf_shoff + elf_shentsize * shnum;
		get_vga_stream() << "  ELF section header " << shnum << " (";
		uint32_t sh_name_off = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x00]), elf_data);
		char *sh_name = reinterpret_cast<char*>(buffer + elf_shstr_off + sh_name_off);
		get_vga_stream() << sh_name << ") ";
		uint32_t sh_type = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x04]), elf_data);
		get_vga_stream() << "type: 0x" << hex << sh_type << dec << " ";
		uint32_t sh_flags = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x08]), elf_data);
		get_vga_stream() << "flags: 0x" << hex << sh_flags << dec << " ";
		uint32_t sh_address = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x0c]), elf_data);
		get_vga_stream() << "address: 0x" << hex << sh_address << dec << " ";
		uint32_t sh_offset = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x10]), elf_data);
		get_vga_stream() << "offset: 0x" << hex << sh_offset << dec << " ";
		uint32_t sh_size = elf_endian(*reinterpret_cast<uint32_t*>(&buffer[offset + 0x14]), elf_data);
		get_vga_stream() << "size: 0x" << hex << sh_size << dec << "\n";
	}*/

	return error_t::no_error;
}

error_t elfrun_implementation::received_udp4(interface*, uint8_t *payload, size_t length, ipv4addr_t, uint16_t, ipv4addr_t, uint16_t)
{
	bool first_packet = payload[0] & 0x01;
	bool last_packet = payload[0] & 0x02;

	if(first_packet) {
		pos = 0;
		awaiting = true;
	} else if(!awaiting) {
		get_vga_stream() << "  elfrun: ignoring out-of-stream packet\n";
		return error_t::no_error;
	}

	if(pos + length - 1 > sizeof(buffer)) {
		get_vga_stream() << "  elfrun: downloading this binary would cause buffer overflow, ignoring\n";
		awaiting = false;
		return error_t::no_error;
	}

	memcpy(buffer + pos, payload + 1, length - 1);
	pos += length - 1;

	if(last_packet) {
		get_vga_stream() << "  elfrun: transmission complete, running binary\n";
		auto res = run_binary();
		if(res != error_t::no_error) {
			get_vga_stream() << "  elfrun: binary failed: " << res << "\n";
		}
		awaiting = false;
	}

	return error_t::no_error;
}
