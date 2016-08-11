SET(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_VDSO_MODULE_LINKER i686-unknown-cloudabi-ld)

if(CMAKE_VERSION VERSION_LESS "3.6.0")
	INCLUDE(CMakeForceCompiler)
	CMAKE_FORCE_C_COMPILER(i686-unknown-cloudabi-cc Clang)
	CMAKE_FORCE_CXX_COMPILER(i686-unknown-cloudabi-c++ Clang)
else()
	set(CMAKE_C_COMPILER i686-unknown-cloudabi-cc)
	set(CMAKE_CXX_COMPILER i686-unknown-cloudabi-c++)
endif()

set(CMAKE_GLD_LINKER_NAME i686-unknown-cloudabi-gld CACHE STRING "Name of the Binutils linker")
mark_as_advanced(CMAKE_GLD_LINKER_NAME)

find_program(CMAKE_GLD_LINKER i686-unknown-cloudabi-gld)
mark_as_advanced(CMAKE_GLD_LINKER)

if(NOT CMAKE_GLD_LINKER)
	message(FATAL_ERROR "Could not find the GNU LD linker: ${CMAKE_GLD_LINKER_NAME}")
endif()

set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_GLD_LINKER} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
