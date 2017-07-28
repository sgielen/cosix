if(BAREMETAL_ENABLED)
	# CloudABI does not support shared libraries, nor does Grub or any
	# other multiboot loader, but since we build our VDSO module which is
	# linked by hand, we do need to support it in the target. We use our
	# own linker language which invokes the linker directly, so that we can
	# create a shared module -- it seems clang does not allow us to do this
	# in cloudabi target mode.
	set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

	add_library(vdso MODULE ../cloudabi/vdsos/cloudabi_vdso_i686.S)
	set_target_properties(vdso PROPERTIES LINKER_LANGUAGE VDSO)
	set_target_properties(vdso PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vdso.ld")
	set_target_properties(vdso PROPERTIES LINK_FLAGS "-T ${CMAKE_CURRENT_SOURCE_DIR}/vdso.ld")

	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vdso_support.s" INPUT "${CMAKE_CURRENT_SOURCE_DIR}/vdso_support.s.in")
	add_library(vdso_support vdso_support.s vdso_support.h)
	add_dependencies(vdso_support vdso)
	set_source_files_properties(vdso_support.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	set_target_properties(vdso_support PROPERTIES LINKER_LANGUAGE CXX)
endif()

include(ExternalProject)
set(EXTERNAL_BINARY_LIST "")
macro(add_external_binary BINARY_NAME)
	list(APPEND EXTERNAL_BINARY_LIST ${BINARY_NAME})
	ExternalProject_Add(${BINARY_NAME}
		SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${BINARY_NAME}"
		PREFIX "${BINARY_NAME}"
		CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/Toolchain-i686-cloudabi.cmake" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
		STEP_TARGETS install
	)
	# By default, changes in ExternalProjects do not force a rebuild, so we
	# add a 'forcebuild' step that always executes and does a rebuild.
	ExternalProject_Add_Step(${BINARY_NAME} forcebuild
		COMMAND true
		DEPENDERS build install
		ALWAYS 1)

	if(BAREMETAL_ENABLED)
		add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/external_binary_${BINARY_NAME}.o"
			COMMAND ${CMAKE_OBJCOPY} --input-target binary
				--output-target elf32-i386
				--binary-architecture i386
				"${BINARY_NAME}" "${CMAKE_CURRENT_BINARY_DIR}/external_binary_${BINARY_NAME}.o"
			DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${BINARY_NAME}/bin/${BINARY_NAME}" "${BINARY_NAME}-install"
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${BINARY_NAME}/bin"
		)
	endif(BAREMETAL_ENABLED)
endmacro()

add_external_binary(init)
add_external_binary(pipe_test)
add_external_binary(concur_test)
add_external_binary(time_test)
add_external_binary(tmpfs)
add_external_binary(tmptest)
add_external_binary(unixsock_test)
add_external_binary(mmap_test)
add_external_binary(forkfork_test)
add_external_binary(networkd)
add_external_binary(dhclient)
add_external_binary(udptest)
add_external_binary(tcptest)
add_external_binary(pythonshell)
add_external_binary(httpd)

set(CLOUDABI_UNITTEST_BINARY "" CACHE FILEPATH "Path to the CloudABI unittest binary, will be run by init if given")
if(CLOUDABI_UNITTEST_BINARY AND BAREMETAL_ENABLED)
	list(APPEND EXTERNAL_BINARY_LIST "unittests")
	add_custom_command(OUTPUT "unittests"
		COMMAND ${CMAKE_COMMAND} -E copy "${CLOUDABI_UNITTEST_BINARY}" "unittests"
		DEPENDS "${CLOUDABI_UNITTEST_BINARY}")
	add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/external_binary_unittests.o"
		COMMAND ${CMAKE_OBJCOPY} --input-target binary
			--output-target elf32-i386
			--binary-architecture i386
			"unittests" "${CMAKE_CURRENT_BINARY_DIR}/external_binary_unittests.o"
		DEPENDS "unittests"
	)
endif()

if(BAREMETAL_ENABLED)
	# Create a header with definitions for all external binaries
	set(EXTERNAL_BINARY_DEFINITIONS)
	foreach(BINARY_NAME ${EXTERNAL_BINARY_LIST})
		set(EXTERNAL_BINARY_DEFINITIONS "${EXTERNAL_BINARY_DEFINITIONS}DEFINE_BINARY(${BINARY_NAME})\n")
	endforeach()
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/external_binaries.h.in
		${CMAKE_CURRENT_BINARY_DIR}/external_binaries.h @ONLY)

	# Add a support library with all external binaries
	set(EXTERNAL_BINARY_OBJECTS)
	foreach(BINARY_NAME ${EXTERNAL_BINARY_LIST})
		list(APPEND EXTERNAL_BINARY_OBJECTS "${CMAKE_CURRENT_BINARY_DIR}/external_binary_${BINARY_NAME}.o")
	endforeach()
	add_library(external_binaries_support external_binaries.h ${EXTERNAL_BINARY_OBJECTS})
	set_target_properties(external_binaries_support PROPERTIES LINKER_LANGUAGE CXX)
endif()
