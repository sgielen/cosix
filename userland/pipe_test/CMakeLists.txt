cmake_minimum_required(VERSION 3.8.2)

project(cloudos-pipe_test)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(pipe_test pipe_test.cpp)

install(TARGETS pipe_test RUNTIME DESTINATION bin)
