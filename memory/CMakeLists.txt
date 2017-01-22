add_library(memory
	allocation.cpp allocation.hpp
	allocator.cpp allocator.hpp
	page_allocator.cpp page_allocator.hpp
	map_virtual.cpp map_virtual.hpp
	allocation_tracker.cpp allocation_tracker.hpp
	bucketizer.hpp
	segregator.hpp
	smart_ptr.hpp
)
list(APPEND memory_tests
)

if(TESTING_ENABLED)
	add_executable(memory_test ${memory_tests} test/test_main.cpp)
	target_include_directories(memory_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(memory_test memory)
	add_test(NAME memory_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND memory_test)
endif()
