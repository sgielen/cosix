add_library(net
	interface.hpp interface.cpp
	ethernet_interface.hpp ethernet_interface.cpp
	loopback_interface.hpp
	interface_store.hpp interface_store.cpp
	)
list(APPEND net_tests)
target_link_libraries(net oslibc)

if(TESTING_ENABLED)
	add_executable(net_test ${net_tests} test/test_main.cpp)
	target_include_directories(net_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(net_test oslibc)
	add_test(NAME net_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND net_test)
endif()
