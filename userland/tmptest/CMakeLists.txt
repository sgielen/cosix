cmake_minimum_required(VERSION 2.8.12)

project(cloudos-tmptest)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(tmptest tmptest.cpp)

install(TARGETS tmptest RUNTIME DESTINATION bin)
