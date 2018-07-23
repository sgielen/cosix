#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <blockdev/blockdev.hpp>
#include <concur/cv.hpp>
#include <stdint.h>

namespace cloudos {

struct x86_ata;

/**
 * This device represents a disk connected to an ATA controller.
 */
struct x86_ata_device : public device, public blockdev {
	x86_ata_device(x86_ata *controller, int io_port, bool master, uint16_t *identifydata);

	const char *description() override;
	cloudabi_errno_t init() override;

	cloudabi_errno_t read_sectors(void *str, uint64_t lba, uint64_t sectorcount) override;
	cloudabi_errno_t write_sectors(const void *str, uint64_t lba, uint64_t sectorcount) override;

private:
	x86_ata *controller;
	uint16_t identifydata[256];
	char description_with_name[40];
	int io_port;
	bool master;
};

/**
 * This device represents the two x86 ATA controllers.
 */
struct x86_ata : public device, public irq_handler {
	x86_ata(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;

	cloudabi_errno_t read_sectors(int io_port, bool master, uint64_t lba, uint64_t sectorcount, void *str);
	cloudabi_errno_t write_sectors(int io_port, bool master, uint64_t lba, uint64_t sectorcount, const void *str);

private:
	void select_bus_device(int port, bool lba, bool master);
	bool identify_bus_device(int port, bool master, uint16_t *data);

	uint8_t busa_selected = 0;
	uint8_t busb_selected = 0;

	bool locked = false;
	cv_t unlocked_cv;
};

}
