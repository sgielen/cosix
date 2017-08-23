cmake_minimum_required(VERSION 3.8.2)

project(cosix-ircclient)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_executable(ircclient
	main.cpp
	terminal.cpp terminal.hpp
	session.cpp session.hpp
	sessionview.cpp sessionview.hpp
	../../term/escape_codes.cpp ../../term/escape_codes.hpp
)
target_compile_definitions(ircclient PRIVATE COSIX_USERLAND)
target_include_directories(ircclient PUBLIC ../..)
target_link_libraries(ircclient -lircclient)

install(TARGETS ircclient RUNTIME DESTINATION bin)

