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
    src/cpu/z80/z80.cpp
)
target_include_directories(model1_cpu PUBLIC src)

add_library(model1_board STATIC
    src/board/rom_set.cpp
    src/board/games/virtua_racing.cpp
    src/board/games/virtua_fighter.cpp
    src/bus/model1_bus.cpp
)
target_include_directories(model1_board PUBLIC src)
target_link_libraries(model1_board PUBLIC model1_cpu)

add_library(model1_video STATIC
    src/video/geometrizer.cpp
    src/video/framebuffer.cpp
)
target_include_directories(model1_video PUBLIC src)

add_executable(model1emu
    src/main.cpp
)
target_include_directories(model1emu PRIVATE src)
target_link_libraries(model1emu PRIVATE model1_board model1_video)

enable_testing()
add_executable(model1_unit_tests
    tests/unit/main.cpp
    tests/unit/v60_test.cpp
    tests/unit/rom_set_test.cpp
    tests/unit/model1_bus_test.cpp
    tests/unit/geometrizer_test.cpp
    tests/unit/framebuffer_test.cpp
    tests/unit/z80_test.cpp
)
target_include_directories(model1_unit_tests PRIVATE third_party/doctest)
target_link_libraries(model1_unit_tests PRIVATE model1_board model1_video)
add_test(NAME model1_unit_tests COMMAND model1_unit_tests)
