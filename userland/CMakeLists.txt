if(BAREMETAL_ENABLED)
	# CloudABI does not support shared libraries, nor does Grub or any
	# other multiboot loader, but since we build our VDSO module which is
	# linked by hand, we do need to support it in the target. We use our
	# own linker language which invokes the linker directly, so that we can
	# create a shared module -- it seems clang does not allow us to do this
	# in cloudabi target mode.
	set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

	add_library(vdso MODULE vdso.c)
	set(CMAKE_VDSO_CREATE_SHARED_MODULE "${CMAKE_VDSO_MODULE_LINKER} <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
	set_target_properties(vdso PROPERTIES LINKER_LANGUAGE VDSO)

	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vdso_support.s" INPUT "${CMAKE_CURRENT_SOURCE_DIR}/vdso_support.s.in")
	add_library(vdso_support vdso_support.s vdso_support.h)
	add_dependencies(vdso_support vdso)
	set_source_files_properties(vdso_support.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	set_target_properties(vdso_support PROPERTIES LINKER_LANGUAGE CXX)

	add_executable(process.bin process_main.cpp syscall.s)
	target_link_libraries(process.bin compiler_rt_builtins)
	set_target_properties(process.bin PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
	set_target_properties(process.bin PROPERTIES LINK_FLAGS "-T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
endif()
