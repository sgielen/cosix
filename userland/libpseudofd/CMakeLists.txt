cmake_minimum_required(VERSION 2.8.12)

project(libpseudofd)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_library(pseudofd reversefd.cpp reverse.cpp cosix/reverse.hpp)
target_include_directories(pseudofd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
