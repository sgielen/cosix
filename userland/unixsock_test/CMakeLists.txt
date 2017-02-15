cmake_minimum_required(VERSION 2.8.12)

project(cloudos-unixsock_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(unixsock_test unixsock_test.cpp)

install(TARGETS unixsock_test RUNTIME DESTINATION bin)
