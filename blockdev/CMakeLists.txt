if(BAREMETAL_ENABLED)
	add_library(blockdev
		blockdev_store.hpp blockdev_store.cpp
		blockdev.hpp blockdev.cpp
	)
endif()
