add_library(memory
	allocator.cpp allocator.hpp
)
list(APPEND memory_tests
	test/test_allocator.cpp
)

if(TESTING_ENABLED)
	add_executable(memory_test ${memory_tests} test/test_main.cpp)
	target_include_directories(memory_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(memory_test memory)
	add_test(NAME memory_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND memory_test)
endif()
