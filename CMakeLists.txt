cmake_minimum_required(VERSION 2.8.12)

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
	add_definitions(-DTESTING_ENABLED)
	set(TESTING_CATCH_INCLUDE ${CMAKE_SOURCE_DIR}/catch/include)
	enable_testing()
endif()

if(BAREMETAL_ENABLED)
	set(C_AND_CXX_FLAGS "-ffreestanding -O0 -g -mno-sse -mno-mmx -fno-sanitize=safe-stack -Wno-reserved-id-macro")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_AND_CXX_FLAGS} -fno-exceptions -fno-rtti")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_AND_CXX_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g -nostdlib")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -O0 -g -nostdlib")
endif()

add_subdirectory(oslibc)
add_subdirectory(hw)
add_subdirectory(net)
add_subdirectory(compiler-rt)
add_subdirectory(userland)
add_subdirectory(fd)
add_subdirectory(memory)

if(BAREMETAL_ENABLED)
	include_directories(${CMAKE_BINARY_DIR})
	set_source_files_properties(boot.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	add_executable(cloudkernel boot.s kernel_main.cpp)
	target_link_libraries(cloudkernel compiler_rt_builtins oslibc hw fd memory net vdso_support)
	set_target_properties(cloudkernel PROPERTIES LINK_DEPENDS "${CMAKE_SOURCE_DIR}/linker.ld")
	set_target_properties(cloudkernel PROPERTIES LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/linker.ld")

	add_custom_target(boot
		COMMAND qemu-system-i386 -kernel cloudkernel -net nic,model=virtio -net dump,file=dump.pcap -net user
		DEPENDS cloudkernel
	)
	add_custom_target(gdbboot
		COMMAND qemu-system-i386 -kernel cloudkernel -net nic,model=virtio -net dump,file=dump.pcap -net user -watchdog-action debug -S -s
		DEPENDS cloudkernel
	)
endif()
