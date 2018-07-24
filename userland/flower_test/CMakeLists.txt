cmake_minimum_required(VERSION 3.8.2)

project(cloudos-flower_test CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../wubwubcmake/warning_settings.cmake)
include(../../wubwubcmake/sanitizers.cmake)
add_sane_warning_flags()

include(../../cmake/aprotoc.cmake)
add_aprotoc(configuration)

add_executable(flower_test flower_test.cpp configuration.ad.h)
target_include_directories(flower_test SYSTEM PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(flower_test PRIVATE arpc)

install(TARGETS flower_test RUNTIME DESTINATION bin)
