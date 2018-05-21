cmake_minimum_required(VERSION 3.8.2)

project(CLOUDOS C CXX ASM)

include(wubwubcmake/enable_cpp11.cmake)
include(wubwubcmake/warning_settings.cmake)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-packed")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-packed")
include(wubwubcmake/version.cmake)
add_sane_warning_flags()
generate_git_version_h("cloudos" "0.1")

include(cmake/arch.cmake)
get_testing_baremetal_enabled(TESTING_ENABLED BAREMETAL_ENABLED)

include_directories(${CMAKE_SOURCE_DIR})

if(TESTING_ENABLED)
	# forcibly enable ASAN if possible with this compiler
	include(wubwubcmake/flag_compiles.cmake)
	set(flags "-fsanitize=address" "-faddress-sanitizer")
	flag_compiles(FLAGS ${flags} WORKING_FLAG asan_flag)
	if(asan_flag)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${asan_flag}")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${asan_flag}")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${asan_flag}")
		set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${asan_flag}")
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${asan_flag}")
	endif()

	add_definitions(-DTESTING_ENABLED)
	set(TESTING_CATCH_INCLUDE ${CMAKE_SOURCE_DIR}/contrib/catch/include)
	include_directories(${CMAKE_SOURCE_DIR}/cloudabi/headers)
	enable_testing()
endif()

if(BAREMETAL_ENABLED)
	set(C_AND_CXX_FLAGS "-ffreestanding -O0 -g -mno-sse -mno-mmx -fno-sanitize=safe-stack -Wno-reserved-id-macro")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_AND_CXX_FLAGS} -fno-exceptions")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_AND_CXX_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g -nostdlib")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -O0 -g -nostdlib")
endif()

add_subdirectory(contrib/compiler-rt)
add_subdirectory(contrib/libcxxabi)
add_subdirectory(oslibc)
add_subdirectory(hw)
add_subdirectory(net)
add_subdirectory(fd)
add_subdirectory(memory)
add_subdirectory(concur)
add_subdirectory(rng)
add_subdirectory(time)
add_subdirectory(proc)
add_subdirectory(term)
add_subdirectory(blockdev)
add_subdirectory(userland)

if(BAREMETAL_ENABLED)
	include_directories(${CMAKE_BINARY_DIR})
	set_source_files_properties(boot.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	add_executable(cloudkernel boot.s kernel_main.cpp)
	target_link_libraries(cloudkernel compiler_rt_builtins cxxabi_rtti oslibc hw fd memory net vdso_support external_binaries_support concur rng time proc fd term blockdev)
	set_target_properties(cloudkernel PROPERTIES LINK_DEPENDS "${CMAKE_SOURCE_DIR}/linker.ld")
	set_target_properties(cloudkernel PROPERTIES LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/linker.ld")

	add_custom_command(OUTPUT disk.img
		COMMAND ${CMAKE_SOURCE_DIR}/misc/disk/make_rootdisk.sh
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	)

	set(QEMU_ARGS -m 1024
		-net nic,model=e1000 -net dump,file=dump.pcap -net user
		-no-reboot -no-shutdown -d int -d cpu_reset -d guest_errors
		-redir tcp:2626::26 -redir tcp:8080::80
		-serial file:kernel.log
		-drive file=disk.img,index=0,format=raw,media=disk
		-append "cosix.config=ext2@ata0p0"
	)

	add_custom_target(boot
		COMMAND qemu-system-i386 -kernel cloudkernel -initrd initrd ${QEMU_ARGS}
		DEPENDS cloudkernel initrd disk.img
	)
	add_custom_target(gdbboot
		COMMAND qemu-system-i386 -kernel cloudkernel -initrd initrd ${QEMU_ARGS} -watchdog-action debug -S -s
		DEPENDS cloudkernel initrd disk.img
	)

	add_custom_target(fastboot
		COMMAND qemu-system-i386 -kernel cloudkernel -initrd initrd ${QEMU_ARGS}
		DEPENDS disk.img
	)
endif()

add_subdirectory(misc)
