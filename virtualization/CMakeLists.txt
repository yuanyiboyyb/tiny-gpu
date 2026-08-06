cmake_minimum_required(VERSION 3.20)

project(tinygpu_virtualization LANGUAGES CXX)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

include(CTest)

option(TINYGPU_BUILD_DEVICE "Build the Verilated tiny-gpu device model" ON)

if(TINYGPU_BUILD_DEVICE)
    add_subdirectory(device)
endif()

if(BUILD_TESTING AND TINYGPU_BUILD_DEVICE)
    add_subdirectory(test)
endif()

add_subdirectory(compiler)
add_subdirectory(runtime)
