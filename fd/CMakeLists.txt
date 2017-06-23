if(BAREMETAL_ENABLED)
	add_library(fd
		fd.hpp
		memory_fd.cpp memory_fd.hpp
		mem_mapping.cpp mem_mapping.hpp
		process_fd.cpp process_fd.hpp
		scheduler.cpp scheduler.hpp scheduler.s
		procfs.cpp procfs.hpp
		bootfs.cpp bootfs.hpp
		initrdfs.cpp initrdfs.hpp
		thread.cpp thread.hpp
		pipe_fd.cpp pipe_fd.hpp
		pseudo_fd.cpp pseudo_fd.hpp
		sock.cpp sock.hpp
		unixsock.cpp unixsock.hpp
		ifstoresock.cpp ifstoresock.hpp
		rawsock.cpp rawsock.hpp
	)

	# for elf.h:
	target_include_directories(fd SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/cloudlibc/src/include")

	# for external_binaries.h:
	target_include_directories(fd PRIVATE "${CMAKE_BINARY_DIR}")
endif()
