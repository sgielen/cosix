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
