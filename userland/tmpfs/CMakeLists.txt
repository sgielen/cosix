cmake_minimum_required(VERSION 3.8.2)

project(cloudos-tmpfs CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
include(../../wubwubcmake/sanitizers.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)

add_executable(tmpfs main.cpp tmpfs.cpp tmpfs.hpp)
target_link_libraries(tmpfs pseudofd)

install(TARGETS tmpfs RUNTIME DESTINATION bin)
