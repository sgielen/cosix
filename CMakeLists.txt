cmake_minimum_required(VERSION 2.8.12)

project(CLOUDOS)

include(wubwubcmake/enable_cpp11.cmake)
include(wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

set(TESTING_CATCH_INCLUDE ${CMAKE_SOURCE_DIR}/catch/include)
enable_testing()

add_subdirectory(oslibc)
add_subdirectory(hw)
