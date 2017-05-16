cmake_minimum_required(VERSION 2.8.12)

project(cloudos-tcptest)

include(../../wubwubcmake/enable_cpp14.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(tcptest tcptest.cpp)

install(TARGETS tcptest RUNTIME DESTINATION bin)
