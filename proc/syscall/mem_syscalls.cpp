#include <proc/syscalls.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

cloudabi_errno_t cloudos::syscall_mem_advise(syscall_context &c)
{
	auto args = arguments_t<void*, size_t, cloudabi_advice_t>(c);
	auto address = args.first();
	auto len = args.second();
	auto advice = args.third();

	if(advice < CLOUDABI_ADVICE_DONTNEED || advice > CLOUDABI_ADVICE_WILLNEED) {
		return EINVAL;
	}

	uintptr_t maxbound = UINTPTR_MAX - len;
	if(reinterpret_cast<uintptr_t>(address) > maxbound) {
		// crosses end of address space
		return EINVAL;
	}

	// TODO: check if the address range is allocated by the application?
	// TODO: act upon advice
	return 0;
}

cloudabi_errno_t cloudos::syscall_mem_map(syscall_context &c)
{
	auto args = arguments_t<void*, size_t, cloudabi_mprot_t, cloudabi_mflags_t, cloudabi_fd_t, cloudabi_filesize_t, void**>(c);
	auto address_requested = args.first();
	auto len = args.second();
	auto prot = args.third();
	auto flags = args.fourth();
	auto fdnum = args.fifth();
	auto fdoff = args.sixth();

	if(flags & ~(CLOUDABI_MAP_ANON | CLOUDABI_MAP_FIXED | CLOUDABI_MAP_PRIVATE | CLOUDABI_MAP_SHARED)) {
		// unsupported flags
		return EINVAL;
	}

	if((flags & CLOUDABI_MAP_PRIVATE) && (flags & CLOUDABI_MAP_SHARED)) {
		// can't be both private and shared
		return EINVAL;
	}

	if(!(flags & CLOUDABI_MAP_PRIVATE) && !(flags & CLOUDABI_MAP_SHARED)) {
		// must be either private or shared
		return EINVAL;
	}

	bool shared = flags & CLOUDABI_MAP_SHARED;

	if(flags & CLOUDABI_MAP_ANON && fdnum != CLOUDABI_MAP_ANON_FD) {
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
		// TODO: also do this if the proposed memory map already
		// overlaps with an existing mapping, or let add_mem_mapping
		// do the placement as well
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

	shared_ptr<fd_t> descriptor;
	cloudabi_filesize_t off = 0;
	if(!(flags & CLOUDABI_MAP_ANON)) {
		int rights_needed = CLOUDABI_RIGHT_MEM_MAP;
		if(prot & CLOUDABI_PROT_READ) {
			rights_needed |= CLOUDABI_RIGHT_FD_READ;
		}
		if(prot & CLOUDABI_PROT_WRITE) {
			rights_needed |= CLOUDABI_RIGHT_FD_WRITE;
		}
		if(prot & CLOUDABI_PROT_EXEC) {
			rights_needed |= CLOUDABI_RIGHT_MEM_MAP_EXEC;
		}
		fd_mapping_t *mapping;
		auto res = c.process()->get_fd(&mapping, fdnum, rights_needed);
		if(res != 0) {
			return res;
		}
		descriptor = mapping->fd;
		off = fdoff;
	}

	mem_mapping_t *mapping = allocate<mem_mapping_t>(c.process(), address_requested, len_to_pages(len), descriptor, off, prot, shared);
	c.process()->add_mem_mapping(mapping, fixed);
	c.result = reinterpret_cast<uintptr_t>(address_requested);

	return 0;
}

cloudabi_errno_t cloudos::syscall_mem_protect(syscall_context &c)
{
	auto args = arguments_t<void*, size_t, cloudabi_mprot_t>(c);
	auto addr = args.first();
	auto len = args.second();
	auto prot = args.third();

	if((prot & CLOUDABI_PROT_EXEC) && (prot & CLOUDABI_PROT_WRITE)) {
		// CloudABI enforces W xor X
		return ENOTSUP;
	}
	if(prot & ~(CLOUDABI_PROT_EXEC | CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE)) {
		// invalid protection bits
		return ENOTSUP;
	}

	c.process()->mem_protect(addr, len_to_pages(len), prot);
	return 0;
}

cloudabi_errno_t cloudos::syscall_mem_sync(syscall_context &c)
{
	auto args = arguments_t<void*, size_t, cloudabi_msflags_t>(c);
	auto addr = args.first();
	auto len = args.second();
	auto flags = args.third();

	if((flags & CLOUDABI_MS_ASYNC) && flags != CLOUDABI_MS_ASYNC) {
		// ASYNC may not be combined with other flags
		return EINVAL;
	}

	if(!(flags & CLOUDABI_MS_ASYNC) && !(flags & CLOUDABI_MS_SYNC)) {
		// either must be present
		return EINVAL;
	}

	if(flags & ~(CLOUDABI_MS_ASYNC | CLOUDABI_MS_INVALIDATE | CLOUDABI_MS_SYNC)) {
		// invalid sync flags
		return ENOTSUP;
	}

	return c.process()->mem_sync(addr, len_to_pages(len), flags);
}

cloudabi_errno_t cloudos::syscall_mem_unmap(syscall_context &c)
{
	auto args = arguments_t<void*, size_t>(c);
	auto addr = args.first();
	auto len = args.second();

	c.process()->mem_unmap(addr, len_to_pages(len));
	return 0;
}

