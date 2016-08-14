cmake_minimum_required(VERSION 2.8.12)

project(cloudos-exec_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(exec_test exec_test.cpp)

install(TARGETS exec_test RUNTIME DESTINATION bin)
