cmake_minimum_required(VERSION 2.8.12)

project(cloudos-udptest)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libcosix libcosix)

add_executable(udptest udptest.cpp)
target_link_libraries(udptest cosix)

install(TARGETS udptest RUNTIME DESTINATION bin)
