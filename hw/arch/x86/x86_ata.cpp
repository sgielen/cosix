#include "x86_ata.hpp"
#include <blockdev/blockdev_store.hpp>
#include <global.hpp>
#include <hw/cpu_io.hpp>
#include <oslibc/assert.hpp>
#include <oslibc/string.h>

using namespace cloudos;

#define BUSA_IOPORT 0x1F0
#define BUSB_IOPORT 0x170

#define REG_IO_DATA    0x0
#define REG_IO_FEATERR 0x1
#define REG_IO_SECTORS 0x2
#define REG_IO_LBALO   0x3
#define REG_IO_LBAMID  0x4
#define REG_IO_LBAHI   0x5
#define REG_IO_SELECT  0x6
#define REG_IO_CMDSTAT 0x7

#define CMD_READ_SECTORS_EXT 0x24
#define CMD_WRITE_SECTORS_EXT 0x34
#define CMD_CACHE_FLUSH_EXT 0xEA
#define CMD_IDENTIFY 0xEC

#define STATUS_ERR 0x01
#define STATUS_DRQ 0x08
#define STATUS_SRV 0x10
#define STATUS_DF  0x20
#define STATUS_RDY 0x40
#define STATUS_BSY 0x80

#define BUSA_DCR 0x3F7
#define BUSB_DCR 0x376

#define ATA_IO_CMD    0xA0 /* always set for every command, ignored by LBA drives */
#define ATA_IO_LBA    0x40 /* command is LBA28 / LBA48 */
#define ATA_IO_SLAVE  0x10 /* command is meant for slave drive */

static void print_device_status(uint8_t value) {
	get_vga_stream() << hex << value << dec << ": ";
#define S(x) if(value & STATUS_##x) get_vga_stream() << " " #x
	S(ERR);
	S(DRQ);
	S(SRV);
	S(DF);
	S(RDY);
	S(BSY);
}

static void wait_until_ready(int port) {
	size_t attempt = 0;
	uint8_t initial_value = 0;
	while(1) {
		auto value = inb(port + REG_IO_CMDSTAT);
		if(++attempt == 1) {
			initial_value = value;
		}
		if(value & STATUS_BSY) {
			// wait for BSY to clear
			continue;
		}
		if(value & (STATUS_RDY | STATUS_ERR | STATUS_DF)) {
			// drive is done
			return;
		}
		if(attempt == 1000) {
			get_vga_stream() << "Still waiting for RDY. Initially: [";
			print_device_status(initial_value);
			get_vga_stream() << "] Now: [";
			print_device_status(value);
			get_vga_stream() << "]\n";
		}
	}
}

static void wait_until_drq(int port) {
	size_t attempt = 0;
	uint8_t initial_value = 0;
	while(1) {
		auto value = inb(port + REG_IO_CMDSTAT);
		if(++attempt == 1) {
			initial_value = value;
		}
		if(value & STATUS_BSY) {
			// wait for BSY to clear
			continue;
		}
		if(value & (STATUS_DRQ | STATUS_ERR | STATUS_DF)) {
			// drive is done
			return;
		}
		if(attempt == 1000) {
			get_vga_stream() << "Still waiting for DRQ. Initially: [";
			print_device_status(initial_value);
			get_vga_stream() << "] Now: [";
			print_device_status(value);
			get_vga_stream() << "]\n";
		}
	}
}

static void wait_for_device_update(int port) {
	// read the status port 4 times for nothing; this takes 400 ns which
	// should be (according to the ATA spec) enough time for the drive to
	// push its new status to the bus
	inb(port + REG_IO_CMDSTAT);
	inb(port + REG_IO_CMDSTAT);
	inb(port + REG_IO_CMDSTAT);
	inb(port + REG_IO_CMDSTAT);
}

void x86_ata::select_bus_device(int port, bool lba, bool master) {
	assert(locked);
	assert(port == BUSA_IOPORT || port == BUSB_IOPORT);

	uint8_t selection = ATA_IO_CMD | (lba ? ATA_IO_LBA : 0) | (master ? 0 : ATA_IO_SLAVE);

	if(port == BUSA_IOPORT) {
		if(busa_selected == selection) {
			return;
		}
		busa_selected = selection;
	} else if(port == BUSB_IOPORT) {
		if(busb_selected == selection) {
			return;
		}
		busb_selected = selection;
	}

	outb(port + REG_IO_SELECT, selection);
	wait_for_device_update(port);
}

bool x86_ata::identify_bus_device(int port, bool master, uint16_t *data)
{
	assert(locked);
	select_bus_device(port, false, master);

	outb(port + REG_IO_SECTORS, 0);
	outb(port + REG_IO_LBALO, 0);
	outb(port + REG_IO_LBAMID, 0);
	outb(port + REG_IO_LBAHI, 0);
	outb(port + REG_IO_CMDSTAT, CMD_IDENTIFY);

	auto status = inb(port + REG_IO_CMDSTAT);
	if(status == 0) {
		return false;
	}

	wait_until_ready(port);

	if(inb(port + REG_IO_LBAMID) != 0 || inb(port + REG_IO_LBAHI) != 0) {
		// it's not an ATA device
		return false;
	}

	wait_until_ready(port);

	if((status & STATUS_ERR || status & STATUS_DF)) {
		// drive errored
		return false;
	}

	for(int i = 0; i < 256; ++i) {
		data[i] = inw(port + REG_IO_DATA);
	}

	if((data[83] & 0x400) == 0) {
		get_vga_stream() << "Note: An ATA device was detected, but ignored because it does not support LBA48 addressing.\n";
	}

	// return true only if LBA48 is supported, the only method implemented in this driver.
	return data[83] & 0x400;
}

x86_ata::x86_ata(device *parent) : device(parent), irq_handler() {
}

const char *x86_ata::description() {
	return "x86 ATA controllers";
}

cloudabi_errno_t x86_ata::init() {
	assert(!locked);
	locked = true;

	register_irq(14);
	register_irq(15);

	// Find devices
	uint16_t identifydata[256];
	cloudabi_errno_t res = 0;
	if(identify_bus_device(BUSA_IOPORT, true, identifydata)) {
		auto disk = make_shared<x86_ata_device>(this, BUSA_IOPORT, true, identifydata);
		disk->init();
		res = get_blockdev_store()->register_blockdev(disk, "ata");
		if(res != 0) {
			return res;
		}
	}
	if(identify_bus_device(BUSA_IOPORT, false, identifydata)) {
		auto disk = make_shared<x86_ata_device>(this, BUSA_IOPORT, false, identifydata);
		disk->init();
		res = get_blockdev_store()->register_blockdev(disk, "ata");
		if(res != 0) {
			return res;
		}
	}
	if(identify_bus_device(BUSB_IOPORT, true, identifydata)) {
		auto disk = make_shared<x86_ata_device>(this, BUSB_IOPORT, true, identifydata);
		disk->init();
		res = get_blockdev_store()->register_blockdev(disk, "ata");
		if(res != 0) {
			return res;
		}
	}
	if(identify_bus_device(BUSB_IOPORT, false, identifydata)) {
		auto disk = make_shared<x86_ata_device>(this, BUSB_IOPORT, false, identifydata);
		disk->init();
		res = get_blockdev_store()->register_blockdev(disk, "ata");
		if(res != 0) {
			return res;
		}
	}

	locked = false;
	unlocked_cv.notify();
	return 0;
}

void x86_ata::handle_irq(uint8_t i) {
	//get_vga_stream() << "Got IRQ " << i << " from ATA controller\n";
	// TODO: handle it
}

cloudabi_errno_t x86_ata::read_sectors(int io_port, bool master, uint64_t lba, uint64_t sectorcount, void *buf)
{
	if(sectorcount == 65536) {
		// special value
		sectorcount = 0;
	} else if(sectorcount > 65536) {
		// can't read this many sectors in one go
		return EINVAL;
	}

	if(lba >= (1LL << 48)) {
		// can't address this sector
		return EINVAL;
	}

	assert(lba <= 0xffffffffffff);
	assert(sectorcount <= 0xffff);

	// TODO: this works until we are multi-processor
	while(locked) {
		unlocked_cv.wait();
	}
	locked = true;

	select_bus_device(io_port, true, master);
	wait_until_ready(io_port);
	outb(io_port + REG_IO_SECTORS, sectorcount >> 16);
	outb(io_port + REG_IO_LBALO,   (lba >> 24) & 0xff);
	outb(io_port + REG_IO_LBAMID,  (lba >> 32) & 0xff);
	outb(io_port + REG_IO_LBAHI,   (lba >> 40) & 0xff);
	outb(io_port + REG_IO_SECTORS, sectorcount & 0xff);
	outb(io_port + REG_IO_LBALO,   (lba      ) & 0xff);
	outb(io_port + REG_IO_LBAMID,  (lba >> 8 ) & 0xff);
	outb(io_port + REG_IO_LBAHI,   (lba >> 16) & 0xff);

	outb(io_port + REG_IO_CMDSTAT, CMD_READ_SECTORS_EXT);

	char *str = reinterpret_cast<char*>(buf);
	for(size_t i = 0; i < sectorcount; ++i) {
		// TODO: block until interrupt here
		wait_until_ready(io_port);

		for(int c = 0; c < 256; ++c) {
			uint16_t v = inw(io_port + REG_IO_DATA);
			(*str++) = v & 0xff;
			(*str++) = v >> 8;
		}

		wait_for_device_update(io_port);
	}

	locked = false;
	unlocked_cv.notify();
	return 0;
}

cloudabi_errno_t x86_ata::write_sectors(int io_port, bool master, uint64_t lba, uint64_t sectorcount, const void *buf)
{
	//get_vga_stream() << "write_sectors of " << sectorcount << " sectors\n";
	if(sectorcount == 65536) {
		// special value
		sectorcount = 0;
	} else if(sectorcount > 65536) {
		// can't read this many sectors in one go
		return EINVAL;
	}

	if(lba >= (1LL << 48)) {
		// can't address this sector
		return EINVAL;
	}

	assert(lba <= 0xffffffffffff);
	assert(sectorcount <= 0xffff);

	// TODO: this works until we are multi-processor
	while(locked) {
		unlocked_cv.wait();
	}
	locked = true;

	select_bus_device(io_port, true, master);
	wait_until_ready(io_port);
	outb(io_port + REG_IO_SECTORS, sectorcount >> 16);
	outb(io_port + REG_IO_LBALO,   (lba >> 24) & 0xff);
	outb(io_port + REG_IO_LBAMID,  (lba >> 32) & 0xff);
	outb(io_port + REG_IO_LBAHI,   (lba >> 40) & 0xff);
	outb(io_port + REG_IO_SECTORS, sectorcount & 0xff);
	outb(io_port + REG_IO_LBALO,   (lba      ) & 0xff);
	outb(io_port + REG_IO_LBAMID,  (lba >> 8 ) & 0xff);
	outb(io_port + REG_IO_LBAHI,   (lba >> 16) & 0xff);

	outb(io_port + REG_IO_CMDSTAT, CMD_WRITE_SECTORS_EXT);

	auto *str = reinterpret_cast<const uint8_t*>(buf);
	for(size_t i = 0; i < sectorcount; ++i) {
		//get_vga_stream() << "write sector " << i << "/" << sectorcount << ": B";
		// TODO: block until interrupt here
		wait_until_drq(io_port);

		//get_vga_stream() << "C";
		for(int c = 0; c < 256; ++c) {
			uint16_t v = uint16_t(str[0]) + (str[1] << 8);
			outw(io_port + REG_IO_DATA, v);
			str += 2;
		}

		//get_vga_stream() << "W";
		wait_for_device_update(io_port);
		//get_vga_stream() << "\n";
	}

	// TODO: we should FLUSH CACHE here, but in Qemu, that command never succeeds,
	// BSY is never cleared
	//outb(io_port + REG_IO_CMDSTAT, CMD_CACHE_FLUSH_EXT);
	//wait_for_device_update(io_port);
	//wait_until_ready(io_port);

	locked = false;
	unlocked_cv.notify();
	return 0;
}

x86_ata_device::x86_ata_device(x86_ata *c, int p, bool m, uint16_t *d)
: ::device(c)
, controller(c)
, io_port(p)
, master(m)
{
	description_with_name[0] = 0;
	strncpy(description_with_name, "x86 ATA storage device ", sizeof(description_with_name));
	memcpy(identifydata, d, sizeof(identifydata));
}

const char *x86_ata_device::description() {
	description_with_name[23] = 0;
	strlcat(description_with_name, get_name(), sizeof(description_with_name));
	return description_with_name;
}

cloudabi_errno_t x86_ata_device::init() {
	return 0;
}

cloudabi_errno_t x86_ata_device::read_sectors(void *str, uint64_t lba, uint64_t sectorcount) {
	return controller->read_sectors(io_port, master, lba, sectorcount, str);
}

cloudabi_errno_t x86_ata_device::write_sectors(const void *str, uint64_t lba, uint64_t sectorcount) {
	return controller->write_sectors(io_port, master, lba, sectorcount, str);
}
