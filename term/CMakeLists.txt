add_library(term
	escape_codes.cpp escape_codes.hpp
)
target_link_libraries(term oslibc)

list(APPEND term_tests
	test/test_escape_codes.cpp
)

if(TESTING_ENABLED)
	add_executable(term_test ${term_tests} test/test_main.cpp)
	target_include_directories(term_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(term_test term)
	add_test(NAME term_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND term_test)
endif()
