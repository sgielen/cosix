SET(CMAKE_SYSTEM_NAME Generic)

if(CMAKE_VERSION VERSION_LESS "3.6.0")
	INCLUDE(CMakeForceCompiler)
	CMAKE_FORCE_C_COMPILER(i686-unknown-cloudabi-cc Clang)
	CMAKE_FORCE_CXX_COMPILER(i686-unknown-cloudabi-c++ Clang)
else()
	set(CMAKE_C_COMPILER i686-unknown-cloudabi-cc)
	set(CMAKE_CXX_COMPILER i686-unknown-cloudabi-c++)
endif()

set(OBJCOPY_NAME "i686-elf-objcopy" CACHE STRING "Name of the objcopy command")
mark_as_advanced(OBJCOPY_NAME)
find_program(OBJCOPY_COMMAND ${OBJCOPY_NAME})
mark_as_advanced(${OBJCOPY_COMMAND})

if(NOT OBJCOPY_COMMAND)
	message(FATAL_ERROR "Could not find objcopy command: ${OBJCOPY_NAME}")
endif()

set(CMAKE_GLD_LINKER_NAME i686-elf-ld CACHE STRING "Name of the Binutils linker")
mark_as_advanced(CMAKE_GLD_LINKER_NAME)

find_program(CMAKE_GLD_LINKER ${CMAKE_GLD_LINKER_NAME})
mark_as_advanced(CMAKE_GLD_LINKER)

if(NOT CMAKE_GLD_LINKER)
	message(FATAL_ERROR "Could not find the GNU LD linker: ${CMAKE_GLD_LINKER_NAME}")
endif()

set(CMAKE_VDSO_MODULE_LINKER ${CMAKE_GLD_LINKER})
set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_GLD_LINKER} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

set(C_AND_CXX_FLAGS "-ffreestanding -O0 -g -mno-sse -mno-mmx -fno-sanitize=safe-stack -Wno-reserved-id-macro")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_AND_CXX_FLAGS} -fno-exceptions -fno-rtti")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_AND_CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g -nostdlib")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -O0 -g")

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
