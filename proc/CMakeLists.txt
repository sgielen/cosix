if(BAREMETAL_ENABLED)
	include_directories(${CMAKE_BINARY_DIR})
	add_library(proc
		syscall_context.hpp
		syscalls.hpp
		syscall/clock_syscalls.cpp
	)
endif()
