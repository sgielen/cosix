add_library(hw vga.cpp vga.hpp)
list(APPEND hw_tests test/test_vga.cpp)

if(TESTING_ENABLED)
	add_executable(hw_test ${hw_tests} test/test_main.cpp)
	target_include_directories(hw_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(hw_test hw)
	add_test(NAME hw_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND hw_test)
endif()
