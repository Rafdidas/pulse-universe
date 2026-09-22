find_package(Catch2 3 CONFIG REQUIRED)

add_executable(pulse-tests
  test_smoke.cpp
)
target_include_directories(pulse-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pulse-tests PRIVATE Catch2::Catch2WithMain)

add_test(NAME pulse-tests COMMAND pulse-tests)
