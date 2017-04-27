cmake_minimum_required(VERSION 2.8.12)

project(cloudos-networkd)

include(../../wubwubcmake/enable_cpp11.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(networkd
	networkd.cpp
	client.hpp client.cpp
)

install(TARGETS networkd RUNTIME DESTINATION bin)
