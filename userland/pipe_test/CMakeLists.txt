cmake_minimum_required(VERSION 2.8.12)

project(cloudos-pipe_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(pipe_test pipe_test.cpp)

install(TARGETS pipe_test RUNTIME DESTINATION bin)
