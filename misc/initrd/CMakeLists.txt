# TODO: make it possible for other CMake files to add stuff to the
# initrd. For now, we hardcode a list of things here:
set(CLOUDABI_PYTHON_BINARY "" CACHE FILEPATH "Path to the CloudABI Python 3 binary")
set(CLOUDABI_PYTHON_LIBRARIES "" CACHE FILEPATH "Path to the CloudABI Python 3 module directory")

if(CLOUDABI_PYTHON_BINARY AND CLOUDABI_PYTHON_LIBRARIES AND BAREMETAL_ENABLED)
	add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/initrd"
		COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/produce_initrd.sh"
			"initrd"
			"${CLOUDABI_PYTHON_BINARY}"
			"--into" "lib"
			"${CLOUDABI_PYTHON_LIBRARIES}"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
	add_custom_target(initrd
		DEPENDS "${CMAKE_BINARY_DIR}/initrd"
	)
endif()
