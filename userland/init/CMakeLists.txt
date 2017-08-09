cmake_minimum_required(VERSION 2.8.12)

project(cloudos-init)

include(../../wubwubcmake/enable_cpp14.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)
add_subdirectory(../libcosix libcosix)

add_executable(init init.cpp)
target_link_libraries(init cosix pseudofd)

install(TARGETS init RUNTIME DESTINATION bin)
