cmake_minimum_required(VERSION 3.10)
project(CinemixAutomationBridge CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_library(cinemix_core STATIC
  src/AutomationEngine.cpp
  src/CaptureFile.cpp
  src/CinemixProtocol.cpp
  src/MidiParser.cpp
  src/MixerProfile.cpp
  src/ParameterMap.cpp
  src/TestModeAnimator.cpp
  src/TouchModeTracker.cpp
  src/TransmissionScheduler.cpp
)
target_include_directories(cinemix_core PUBLIC include)
target_compile_features(cinemix_core PUBLIC cxx_std_11)

find_package(Threads REQUIRED)
target_link_libraries(cinemix_core PUBLIC Threads::Threads)

enable_testing()
add_subdirectory(tests)
