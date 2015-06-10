cmake_minimum_required(VERSION 2.8.12)

project(CLOUDOS C CXX ASM)

include(wubwubcmake/enable_cpp11.cmake)
include(wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

include(cmake/arch.cmake)
get_testing_baremetal_enabled(TESTING_ENABLED BAREMETAL_ENABLED)

include_directories(${CMAKE_SOURCE_DIR})

if(TESTING_ENABLED)
	set(TESTING_CATCH_INCLUDE ${CMAKE_SOURCE_DIR}/catch/include)
	enable_testing()
endif()

add_subdirectory(oslibc)
add_subdirectory(hw)

if(BAREMETAL_ENABLED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffreestanding -O0 -g -fno-exceptions -fno-rtti -I${CMAKE_BUILD_DIR}")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffreestanding -O0 -g")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g -nostdlib -T ${CMAKE_SOURCE_DIR}/linker.ld")
	set_source_files_properties(boot.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	add_executable(cloudkernel boot.s kernel_main.cpp)
	target_link_libraries(cloudkernel hw oslibc)
	set_target_properties(cloudkernel PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linker.ld)

	add_custom_target(boot qemu-system-i386 -kernel cloudkernel)
endif()
