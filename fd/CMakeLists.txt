if(BAREMETAL_ENABLED)
	include_directories(${CMAKE_BINARY_DIR})
	add_library(fd
		fd.hpp
		memory_fd.cpp memory_fd.hpp
		mem_mapping.cpp mem_mapping.hpp
		process_fd.cpp process_fd.hpp
		scheduler.cpp scheduler.hpp scheduler.s
		procfs.cpp procfs.hpp
		bootfs.cpp bootfs.hpp
		thread.cpp thread.hpp
		pipe_fd.cpp pipe_fd.hpp
		pseudo_fd.cpp pseudo_fd.hpp
		socket_fd.cpp socket_fd.hpp
	)
endif()
