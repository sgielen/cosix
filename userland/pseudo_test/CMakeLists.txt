cmake_minimum_required(VERSION 2.8.12)

project(cloudos-pseudo_test)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
add_sane_warning_flags()

add_subdirectory(../libpseudofd libpseudofd)

add_executable(pseudo_test
	pseudo_test.cpp
)
target_link_libraries(pseudo_test pseudofd)

install(TARGETS pseudo_test RUNTIME DESTINATION bin)
