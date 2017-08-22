cmake_minimum_required(VERSION 2.8.12)

project(cloudos-networkd)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)

add_executable(networkd
	networkd.cpp networkd.hpp
	util.hpp
	client.hpp client.cpp
	interface.hpp interface.cpp
	arp.hpp arp.cpp
	routing_table.hpp routing_table.cpp
	ip.hpp ip.cpp
	ip_socket.hpp ip_socket.cpp
	tcp_socket.hpp tcp_socket.cpp
	udp_socket.hpp udp_socket.cpp
)
target_link_libraries(networkd pseudofd)

install(TARGETS networkd RUNTIME DESTINATION bin)
