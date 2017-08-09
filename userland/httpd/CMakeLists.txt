cmake_minimum_required(VERSION 2.8.12)

project(cloudos-httpd)

include(../../wubwubcmake/enable_cpp14.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libcosix libcosix)

add_executable(httpd httpd.cpp)
target_link_libraries(httpd cosix)

install(TARGETS httpd RUNTIME DESTINATION bin)
