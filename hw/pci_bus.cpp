#include "hw/pci_bus.hpp"
#include "hw/driver_store.hpp"
#include "hw/cpu_io.hpp"
#include <oslibc/numeric.h>
#include "global.hpp"

using namespace cloudos;

const char *pci_driver::description() {
	return "PCI root bus driver";
}

device *pci_driver::probe_root_device(device *root) {
	auto *driver = get_allocator()->allocate<pci_bus>();
	new(driver) pci_bus(root);
	return driver;
}

pci_bus::pci_bus(device *parent)
: device(parent)
{}

const char *pci_bus::description()
{
	return "Root PCI bus";
}

cloudabi_errno_t pci_bus::init()
{
	auto *list = get_driver_store()->get_drivers();
	for(uint8_t device_nr = 0; device_nr < 32; ++device_nr) {
		uint16_t device_id = get_device_id(device_nr);
		uint16_t vendor_id = get_vendor_id(device_nr);
		if(device_id == 0xffff && vendor_id == 0xffff) {
			continue;
		}

		device *dev = nullptr;
		find(list, [this, device_nr, &dev](driver_list *item) {
			dev = item->data->probe_pci_device(this, device_nr);
			return dev != nullptr;
		});

		if(dev == nullptr) {
			dev = allocate<pci_unused_device>(this, device_nr);
		}

		auto res = dev->init();
		if(res != 0) {
			return res;
		}
	}
	return 0;
}

uint16_t pci_bus::get_device_id(uint8_t device)
{
	const uint8_t bus = 0;
	uint32_t device_vendor = read_pci_config(bus, device, 0, 0x00);
	return device_vendor >> 16;
}

uint16_t pci_bus::get_vendor_id(uint8_t device)
{
	const uint8_t bus = 0;
	uint32_t device_vendor = read_pci_config(bus, device, 0, 0x00);
	return device_vendor & 0xffff;
}

uint16_t pci_bus::get_subsystem_id(uint8_t device) {
	const uint8_t bus = 0;
	uint32_t subsystem_info = read_pci_config(bus, device, 0, 0x2c);
	return subsystem_info >> 16;
}

uint32_t pci_bus::get_bar0(uint8_t device) {
	const uint8_t bus = 0;
	return read_pci_config(bus, device, 0, 0x10);
}

uint32_t pci_bus::read_pci_config(uint8_t bus, uint8_t device,
	uint8_t function, uint8_t reg)
{
	const int CONFIG_ADDRESS = 0xCF8;
	const int CONFIG_DATA = 0xCFC;

	uint32_t address =
		uint32_t(1)               << 31 |
		uint32_t(bus)             << 16 |
		uint32_t(device & 0x1f)   << 11 |
		uint32_t(function & 0x03) <<  8 |
		uint32_t(reg & 0xfc); // 32-bit align read
	outl(CONFIG_ADDRESS, address);

	return inl(CONFIG_DATA);
}

pci_device::pci_device(pci_bus *b, uint8_t d)
: bus(b)
, dev(d)
{}

template <typename T>
void bar_write(uint8_t bar_type, uint64_t base_address, uint16_t offset, T value) {
	assert(bar_type == 1 || bar_type == 2);
	assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
	if(bar_type == 1) {
		// IO
		switch(sizeof(T)) {
		case 1: outb(base_address + offset, value); break;
		case 2: outw(base_address + offset, value); break;
		case 4: outl(base_address + offset, value); break;
		default: assert(false);
		}
	} else {
		// Memory
		uint8_t *p = reinterpret_cast<uint8_t*>(base_address) + offset;
		*reinterpret_cast<T*>(p) = value;
	}
}

template <typename T>
T bar_read(uint8_t bar_type, uint64_t base_address, uint16_t offset) {
	assert(bar_type == 1 || bar_type == 2);
	assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
	if(bar_type == 1) {
		// IO
		switch(sizeof(T)) {
		case 1: return inb(base_address + offset); break;
		case 2: return inw(base_address + offset); break;
		case 4: return inl(base_address + offset); break;
		default: assert(false);
		}
	} else {
		// Memory
		uint8_t *p = reinterpret_cast<uint8_t*>(base_address) + offset;
		return *reinterpret_cast<T*>(p);
	}
}

void pci_device::write8(uint16_t offset, uint8_t value)
{
	init_pci_device();
	bar_write(bar_type, base_address, offset, value);
}

void pci_device::write16(uint16_t offset, uint16_t value)
{
	init_pci_device();
	bar_write(bar_type, base_address, offset, value);
}

void pci_device::write32(uint16_t offset, uint32_t value)
{
	init_pci_device();
	bar_write(bar_type, base_address, offset, value);
}

uint8_t pci_device::read8(uint16_t offset)
{
	init_pci_device();
	return bar_read<uint8_t>(bar_type, base_address, offset);
}

uint16_t pci_device::read16(uint16_t offset)
{
	init_pci_device();
	return bar_read<uint16_t>(bar_type, base_address, offset);
}

uint32_t pci_device::read32(uint16_t offset)
{
	init_pci_device();
	return bar_read<uint32_t>(bar_type, base_address, offset);
}

void pci_device::init_pci_device()
{
	if(bar_type != 0) {
		return;
	}

	uint32_t bar0 = bus->get_bar0(dev);
	if((bar0 & 0x01) != 1) {
		kernel_panic("Memory mapped BAR types are unsupported.");
		// TODO: the base address is a physical memory address. Set
		// base_address to the mapping in virtual memory.
	}
	bar_type = 1;
	base_address = bar0 & 0xfffffffc;
}

uint16_t pci_device::get_device_id() {
	return bus->get_device_id(dev);
}

uint16_t pci_device::get_vendor_id() {
	return bus->get_vendor_id(dev);
}

uint16_t pci_device::get_subsystem_id() {
	return bus->get_subsystem_id(dev);
}

uint32_t pci_device::get_bar0() {
	return bus->get_bar0(dev);
}

pci_unused_device::pci_unused_device(pci_bus *b, uint8_t d)
: device(b)
, pci_device(b, d)
{}

pci_unused_device::~pci_unused_device()
{
	deallocate(descr);
}

const char *pci_unused_device::description()
{
	return reinterpret_cast<const char*>(descr.ptr);
}

cloudabi_errno_t pci_unused_device::init()
{
	descr = allocate(48);
	char *d = reinterpret_cast<char*>(descr.ptr);
	strncpy(d, "Unused PCI device ", descr.size);

	char buf[8];
	strncat(d, uitoa_s(get_vendor_id(), buf, sizeof(buf), 16), descr.size - strlen(d) - 1);
	strncat(d, ":", descr.size - strlen(d) - 1);
	strncat(d, uitoa_s(get_device_id(), buf, sizeof(buf), 16), descr.size - strlen(d) - 1);
	return 0;
}
