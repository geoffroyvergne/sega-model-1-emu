cmake_minimum_required(VERSION 3.20)
project(sega_model1_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_library(model1_cpu STATIC
    src/cpu/v60/v60.cpp
)
target_include_directories(model1_cpu PUBLIC src)

add_executable(model1emu
    src/main.cpp
)
target_include_directories(model1emu PRIVATE src)
target_link_libraries(model1emu PRIVATE model1_cpu)

enable_testing()
add_executable(model1_unit_tests
    tests/unit/main.cpp
    tests/unit/v60_test.cpp
)
target_include_directories(model1_unit_tests PRIVATE third_party/doctest)
target_link_libraries(model1_unit_tests PRIVATE model1_cpu)
add_test(NAME model1_unit_tests COMMAND model1_unit_tests)
