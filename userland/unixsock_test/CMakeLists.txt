cmake_minimum_required(VERSION 3.8.2)

project(cloudos-unixsock_test)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(unixsock_test unixsock_test.cpp)

install(TARGETS unixsock_test RUNTIME DESTINATION bin)
