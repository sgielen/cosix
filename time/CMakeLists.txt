if(BAREMETAL_ENABLED)
	add_library(time
		clock_store.hpp clock_store.cpp
	)
endif()
