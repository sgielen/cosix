cmake_minimum_required(VERSION 2.8.12)

project(libpseudofd)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_library(pseudofd reversefd.cpp reverse.cpp cosix/reverse.hpp)
target_include_directories(pseudofd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
