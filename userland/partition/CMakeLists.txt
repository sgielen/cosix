cmake_minimum_required(VERSION 3.8.2)

project(cloudos-partition CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
include(../../wubwubcmake/sanitizers.cmake)
add_sane_warning_flags()

add_subdirectory(../libcosix libcosix)

add_executable(partition main.cpp)
target_link_libraries(partition cosix)

install(TARGETS partition RUNTIME DESTINATION bin)
