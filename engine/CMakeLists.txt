cmake_minimum_required(VERSION 3.25)
project(pulse_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
endif()

find_package(Boost REQUIRED COMPONENTS json beast)

add_library(pulse_core
  src/core/CpuDelta.cpp
  src/core/GroupBuilder.cpp
  src/core/ProcessFilter.cpp
  src/core/LifecycleTracker.cpp
  src/core/FlowEstimator.cpp
  src/core/DataAggregator.cpp
  src/app/EngineLoop.cpp
  src/app/ServeApp.cpp
  src/platform/windows/WindowsSystemReader.cpp
  src/cli/TableFormatter.cpp
  src/cli/Options.cpp
  src/network/Serializer.cpp
  src/network/WebSocketServer.cpp
)
target_include_directories(pulse_core PUBLIC src)
target_link_libraries(pulse_core PUBLIC Boost::json Boost::beast)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi ws2_32 mswsock)
endif()

if(WIN32)
  # Asio 가 정의되지 않은 _WIN32_WINNT 를 자기 기본값으로 메운다. 이 매크로는
  # 어떤 소켓 API 가 켜지는지를 고른다 — Boost 를 올릴 때 조용히 바뀌지 않도록
  # 이 프로젝트가 직접 고정한다. 0x0A00 은 Windows 10 이상이다.
  target_compile_definitions(pulse_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

add_executable(pulse-engine src/main.cpp)
target_link_libraries(pulse-engine PRIVATE pulse_core)

enable_testing()
add_subdirectory(tests)
