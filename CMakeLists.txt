cmake_minimum_required(VERSION 3.10)
project(CinemixAutomationBridge CXX)

# Portable core + tests + hardware-free harness.
# The macOS Audio Unit (mac/) builds with make/clang on the target Mac and is
# intentionally NOT part of this CMake tree (see mac/Makefile, docs/BUILDING.md).
enable_testing()
add_subdirectory(core)
add_subdirectory(tools/cinemix_harness)
