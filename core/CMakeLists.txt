cmake_minimum_required(VERSION 3.10)
project(CinemixAutomationBridge CXX)

# First-party portable code is conservative C++14 (docs/BUILDING.md).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Strong first-party warnings. Not -Werror: the goal is to expose suspicious
# conversions and legacy idioms, not to make portability brittle. The vendored
# AudioUnitSDK (mac/) is compiled separately and is NOT subject to these.
set(CINEMIX_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
                     -Wold-style-cast -Wshadow)

add_library(cinemix_core STATIC
  src/AutomationEngine.cpp
  src/CaptureFile.cpp
  src/CinemixProtocol.cpp
  src/MidiParser.cpp
  src/MixerProfile.cpp
  src/ParameterMap.cpp
  src/TouchModeTracker.cpp
  src/TransmissionScheduler.cpp
)
target_include_directories(cinemix_core PUBLIC include)
target_compile_features(cinemix_core PUBLIC cxx_std_14)
target_compile_options(cinemix_core PRIVATE ${CINEMIX_WARNINGS})

find_package(Threads REQUIRED)
target_link_libraries(cinemix_core PUBLIC Threads::Threads)

enable_testing()
add_subdirectory(tests)
