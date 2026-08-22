# Cinemix core test suite — conservative C++14, strong first-party warnings.
set(CINEMIX_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
                     -Wold-style-cast -Wshadow)

set(CINEMIX_TESTS
  test_capture
  test_engine
  test_oscillator
  test_param_map
  test_parser
  test_protocol
  test_ring
  test_scheduler
)

foreach(t ${CINEMIX_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE cinemix_core)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${t} PRIVATE ${CINEMIX_WARNINGS})
  add_test(NAME ${t} COMMAND ${t})
endforeach()
