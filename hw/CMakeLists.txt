if(BAREMETAL_ENABLED)
	set(EXTRA_HW_SOURCES segments.s)
	set_source_files_properties(segments.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
else()
	set(EXTRA_HW_SOURCES test/segments_stub.c)
endif()

add_library(hw
	vga.cpp vga.hpp
	vga_stream.hpp vga_stream.cpp
	multiboot.hpp multiboot.cpp
	segments.hpp segments.cpp
	${EXTRA_HW_SOURCES})
list(APPEND hw_tests
	test/test_vga.cpp
	test/test_vga_stream.cpp
	test/test_multiboot.cpp
	test/test_segments.cpp)
target_link_libraries(hw oslibc)

if(TESTING_ENABLED)
	add_executable(hw_test ${hw_tests} test/test_main.cpp)
	target_include_directories(hw_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(hw_test hw)
	add_test(NAME hw_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND hw_test)
endif()
