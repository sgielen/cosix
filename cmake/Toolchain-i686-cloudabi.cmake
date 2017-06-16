cmake_minimum_required(VERSION 3.6.0)
SET(CMAKE_SYSTEM_NAME Generic)

set(CLOUDABI_TRIPLET i686-unknown-cloudabi)
include(${CMAKE_CURRENT_LIST_DIR}/Toolchain-cloudabi-common.cmake)

# Work-around for clang r264966
if(NOT CLOUDABI_NO_X86_CALL_FRAME_SET)
	set(CLOUDABI_NO_X86_CALL_FRAME_SET 1)
	set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mllvm -no-x86-call-frame-opt")
	set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -mllvm -no-x86-call-frame-opt")
endif()
