#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_mem_advise(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_mem_lock(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_mem_map(syscall_context &c)
{
	auto args = arguments_t<void*, size_t, cloudabi_mprot_t, cloudabi_mflags_t, cloudabi_fd_t, cloudabi_filesize_t, void**>(c);
	auto address_requested = args.first();
	auto len = args.second();
	auto prot = args.third();
	auto flags = args.fourth();
	auto fd = args.fifth();

	if(!(flags & CLOUDABI_MAP_ANON)) {
		get_vga_stream() << "Only anonymous mappings are supported at the moment\n";
		return ENOSYS;
	}
	if(!(flags & CLOUDABI_MAP_PRIVATE)) {
		get_vga_stream() << "Only private mappings are supported at the moment\n";
		return ENOSYS;
	}
	if(flags & CLOUDABI_MAP_ANON && fd != CLOUDABI_MAP_ANON_FD) {
		return EINVAL;
	}
	if((prot & CLOUDABI_PROT_EXEC) && (prot & CLOUDABI_PROT_WRITE)) {
		// CloudABI enforces W xor X
		return ENOTSUP;
	}
	if(prot & ~(CLOUDABI_PROT_EXEC | CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE)) {
		// invalid protection bits
		return ENOTSUP;
	}
	// NOTE: cloudabi sys_mem_map() defines that address_requested
	// is only used when MAP_FIXED is given, but in other mmap()
	// implementations address_requested is considered a hint when
	// MAP_FIXED isn't given. We try that as well.
	// TODO: if !given and address_requested isn't free, also find
	// another free virtual range.
	bool fixed = flags & CLOUDABI_MAP_FIXED;
	if(!fixed && address_requested == nullptr) {
		address_requested = c.process()->find_free_virtual_range(len_to_pages(len));
		if(address_requested == nullptr) {
			get_vga_stream() << "Failed to find virtual memory for mapping.\n";
			return ENOMEM;
		}
	}
	if((reinterpret_cast<uint32_t>(address_requested) % process_fd::PAGE_SIZE) != 0) {
		get_vga_stream() << "Address requested isn't page aligned\n";
		return EINVAL;
	}

	mem_mapping_t *mapping = get_allocator()->allocate<mem_mapping_t>();
	new (mapping) mem_mapping_t(c.process(), address_requested, len_to_pages(len), NULL, 0, prot);
	c.process()->add_mem_mapping(mapping, fixed);
	// TODO: instead of completely backing, await the page fault and do it then
	mapping->ensure_completely_backed();
	memset(address_requested, 0, len);
	c.result = reinterpret_cast<uintptr_t>(address_requested);
	return 0;
}

cloudabi_errno_t cloudos::syscall_mem_protect(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_mem_sync(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_mem_unlock(syscall_context &)
{
	return ENOSYS;
}

cloudabi_errno_t cloudos::syscall_mem_unmap(syscall_context &c)
{
	// find mapping, remove it from list, remove it from page tables
	auto args = arguments_t<void*, size_t>(c);
	auto addr = args.first();
	auto len = args.second();

	c.process()->mem_unmap(addr, len);
	return 0;
}

