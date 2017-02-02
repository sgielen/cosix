cmake_minimum_required(VERSION 2.8.12)

project(libcosixfs)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_library(cosixfs cosixfs.cpp filesystem.cpp cosix/filesystem.hpp)
target_include_directories(cosixfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
