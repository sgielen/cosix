if(BAREMETAL_ENABLED)
	add_library(concur
		cv.hpp cv.cpp
		condition.hpp condition.cpp
	)
endif()
