cmake_minimum_required(VERSION 2.8.12)

project(cloudos-thread_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(thread_test thread_test.cpp)

install(TARGETS thread_test RUNTIME DESTINATION bin)
