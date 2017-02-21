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

pci_unused_device::pci_unused_device(pci_bus *b, uint8_t d)
: device(b)
, bus(b)
, dev(d)
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
	strncat(d, uitoa_s(bus->get_vendor_id(dev), buf, sizeof(buf), 16), descr.size - strlen(d) - 1);
	strncat(d, ":", descr.size - strlen(d) - 1);
	strncat(d, uitoa_s(bus->get_device_id(dev), buf, sizeof(buf), 16), descr.size - strlen(d) - 1);
	return 0;
}
