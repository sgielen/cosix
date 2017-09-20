cmake_minimum_required(VERSION 3.8.2)

project(cloudos-telnetd)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libcosix libcosix)
add_subdirectory(../libpseudofd libpseudofd)

add_executable(telnetd telnetd.cpp tterm.hpp tterm.cpp)
target_link_libraries(telnetd cosix pseudofd arpc)

install(TARGETS telnetd RUNTIME DESTINATION bin)
