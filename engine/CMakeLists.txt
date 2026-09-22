cmake_minimum_required(VERSION 3.25)
project(pulse_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
endif()

add_library(pulse_core
  src/core/CpuDelta.cpp
  src/core/GroupBuilder.cpp
  src/core/ProcessFilter.cpp
  src/core/LifecycleTracker.cpp
  src/core/FlowEstimator.cpp
  src/core/DataAggregator.cpp
  src/platform/windows/WindowsSystemReader.cpp
  src/cli/TableFormatter.cpp
)
target_include_directories(pulse_core PUBLIC src)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi)
endif()

add_executable(pulse-engine src/main.cpp)
target_link_libraries(pulse-engine PRIVATE pulse_core)

enable_testing()
add_subdirectory(tests)
