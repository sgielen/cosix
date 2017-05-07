cmake_minimum_required(VERSION 2.8.12)

project(cloudos-tmpfs)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)

add_executable(tmpfs main.cpp tmpfs.cpp tmpfs.hpp)
target_link_libraries(tmpfs pseudofd)

install(TARGETS tmpfs RUNTIME DESTINATION bin)
