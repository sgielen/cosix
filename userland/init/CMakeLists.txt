cmake_minimum_required(VERSION 3.8.2)

project(cloudos-init)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)
add_subdirectory(../libcosix libcosix)

include(../../cmake/aprotoc.cmake)
add_aprotoc(../flower_test/configuration)

add_executable(init init.cpp ../flower_test/configuration.ad.h)
target_include_directories(init SYSTEM PRIVATE ${CMAKE_BINARY_DIR}/..)
target_link_libraries(init cosix pseudofd arpc)

install(TARGETS init RUNTIME DESTINATION bin)
