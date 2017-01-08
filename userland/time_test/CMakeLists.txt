cmake_minimum_required(VERSION 2.8.12)

project(cloudos-time_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(time_test time_test.cpp)

install(TARGETS time_test RUNTIME DESTINATION bin)
