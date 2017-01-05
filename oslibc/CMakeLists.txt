add_library(oslibc
	string.c string.h
	numeric.c numeric.h
	cxx_support.cpp
	error.h
	in.h
	list.hpp
	assert.cpp assert.hpp
	utility.hpp
	bitmap.cpp bitmap.hpp
)
target_link_libraries(oslibc hw)
list(APPEND oslibc_tests test/test_string.cpp test/test_numeric.cpp test/test_list.cpp)

if(BAREMETAL_ENABLED)
	target_link_libraries(oslibc compiler_rt_builtins)
endif()

if(TESTING_ENABLED)
	add_executable(oslibc_test ${oslibc_tests} test/test_main.cpp)
	target_include_directories(oslibc_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(oslibc_test oslibc)
	add_test(NAME oslibc_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND oslibc_test)
endif()
