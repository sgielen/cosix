if(BAREMETAL_ENABLED)
	add_library(fd
		fd.hpp
		memory_fd.hpp
		process_fd.cpp process_fd.hpp
		scheduler.cpp scheduler.hpp
	)
endif()
