cmake_minimum_required(VERSION 2.8.12)

project(cloudos-concur_test)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(concur_test concur_test.cpp)

install(TARGETS concur_test RUNTIME DESTINATION bin)
