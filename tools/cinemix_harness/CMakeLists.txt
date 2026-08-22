cmake_minimum_required(VERSION 3.10)
project(cinemix_harness CXX)

# First-party portable code is conservative C++14 (docs/BUILDING.md).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cinemix_harness
  main.cpp
  FakeConsole.cpp
)
target_link_libraries(cinemix_harness PRIVATE cinemix_core)
target_include_directories(cinemix_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(cinemix_harness PRIVATE -Wall -Wextra -Wpedantic -Wconversion
                                                 -Wsign-conversion -Wold-style-cast -Wshadow)
