cmake_minimum_required(VERSION 2.8.12)

project(cloudos-mmap_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(mmap_test mmap_test.cpp)

install(TARGETS mmap_test RUNTIME DESTINATION bin)
