cmake_minimum_required(VERSION 3.10)
project(cinemix_harness CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cinemix_harness
  main.cpp
  FakeConsole.cpp
)
target_link_libraries(cinemix_harness PRIVATE cinemix_core)
target_include_directories(cinemix_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
