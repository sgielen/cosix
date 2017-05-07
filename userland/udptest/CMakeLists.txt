cmake_minimum_required(VERSION 2.8.12)

project(cloudos-udptest)

include(../../wubwubcmake/enable_cpp14.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(udptest udptest.cpp)

install(TARGETS udptest RUNTIME DESTINATION bin)
