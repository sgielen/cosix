if(BAREMETAL_ENABLED)
	add_library(userland
		process.cpp process.hpp
		syscall.s
	)
	set_source_files_properties(syscall.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
endif()
