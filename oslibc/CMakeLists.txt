add_library(oslibc string.c string.h)
list(APPEND oslibc_tests test/test_string.cpp)

if(TESTING_ENABLED)
	add_executable(oslibc_test ${oslibc_tests} test/test_main.cpp)
	target_include_directories(oslibc_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(oslibc_test oslibc)
	add_test(NAME oslibc_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND oslibc_test)
endif()
