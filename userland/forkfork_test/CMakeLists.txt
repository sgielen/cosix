cmake_minimum_required(VERSION 2.8.12)

project(cloudos-forkfork_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(forkfork_test forkfork_test.cpp)

install(TARGETS forkfork_test RUNTIME DESTINATION bin)
