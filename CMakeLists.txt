cmake_minimum_required(VERSION 3.20)
project(sega_model1_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_executable(model1emu
    src/main.cpp
)

target_include_directories(model1emu PRIVATE src)
