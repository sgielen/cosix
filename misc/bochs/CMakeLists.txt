if(BAREMETAL_ENABLED)
	add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bochsrc.txt"
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/bochsrc.txt"
			"${CMAKE_BINARY_DIR}/bochsrc.txt"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
	add_custom_target(bochsboot
		COMMAND bochs
		DEPENDS iso "${CMAKE_BINARY_DIR}/bochsrc.txt"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
endif()
