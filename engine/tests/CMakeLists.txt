find_package(Catch2 3 CONFIG REQUIRED)

add_executable(pulse-tests
  test_smoke.cpp
  test_cpu_delta.cpp
  test_group_builder.cpp
  test_process_filter.cpp
  test_lifecycle_tracker.cpp
  test_flow_estimator.cpp
  test_aggregator.cpp
  test_windows_reader.cpp
  test_table_formatter.cpp
  test_options.cpp
)
target_include_directories(pulse-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pulse-tests PRIVATE Catch2::Catch2WithMain pulse_core)

add_test(NAME pulse-tests COMMAND pulse-tests)
