if(BAREMETAL_ENABLED)
	add_library(userland
		process.cpp process.hpp
		syscall.s
	)
	set_source_files_properties(syscall.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")

	add_executable(process.bin process_main.cpp syscall.s)
	set_target_properties(process.bin PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
	set_target_properties(process.bin PROPERTIES LINK_FLAGS "-T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
endif()
