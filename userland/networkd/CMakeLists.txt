cmake_minimum_required(VERSION 2.8.12)

project(cloudos-networkd)

include(../../wubwubcmake/enable_cpp14.cmake)
include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(networkd
	networkd.cpp networkd.hpp
	util.hpp
	client.hpp client.cpp
	interface.hpp interface.cpp
	arp.hpp arp.cpp
)

install(TARGETS networkd RUNTIME DESTINATION bin)
