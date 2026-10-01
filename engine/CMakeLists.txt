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
  src/core/RunTimeTable.cpp
  src/core/MeasuredFlows.cpp
  src/core/DataAggregator.cpp
  src/app/EngineLoop.cpp
  src/app/ServeApp.cpp
  src/platform/windows/WindowsSystemReader.cpp
  src/platform/windows/EtwSchedulerCollector.cpp
  src/cli/TableFormatter.cpp
  src/cli/Options.cpp
  src/network/Serializer.cpp
  src/network/WebSocketServer.cpp
  src/network/StaticFiles.cpp
  src/network/AssetPack.cpp
  src/platform/windows/EmbeddedAssets.cpp
)
target_include_directories(pulse_core PUBLIC src)
target_link_libraries(pulse_core PUBLIC Boost::json Boost::beast)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi ws2_32 mswsock advapi32)
endif()

if(WIN32)
  # Asio 가 정의되지 않은 _WIN32_WINNT 를 자기 기본값으로 메운다. 이 매크로는
  # 어떤 소켓 API 가 켜지는지를 고른다 — Boost 를 올릴 때 조용히 바뀌지 않도록
  # 이 프로젝트가 직접 고정한다. 0x0A00 은 Windows 10 이상이다.
  target_compile_definitions(pulse_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

# 릴리스 빌드는 scripts/make-pak.ps1 이 만든 web.pak 을 넘겨 프런트엔드를 exe 에 넣는다.
# 값이 없으면 (개발 빌드·테스트) 리소스 없이 지금처럼 동작한다.
set(PULSE_WEB_PAK "" CACHE FILEPATH "Packed frontend (web.pak) to embed into pulse-engine")
set(PULSE_ENGINE_SOURCES src/main.cpp)
if(PULSE_WEB_PAK)
  if(NOT EXISTS "${PULSE_WEB_PAK}")
    message(FATAL_ERROR "PULSE_WEB_PAK does not exist: ${PULSE_WEB_PAK}")
  endif()
  # rc 는 경로의 비ASCII 문자와 역슬래시에 약하다. 빌드 폴더로 복사해 상대 이름으로 참조한다.
  configure_file("${PULSE_WEB_PAK}" "${CMAKE_CURRENT_BINARY_DIR}/web.pak" COPYONLY)
  configure_file(resources/web.rc.in "${CMAKE_CURRENT_BINARY_DIR}/web.rc" COPYONLY)
  list(APPEND PULSE_ENGINE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/web.rc")
  # 리소스 재빌드는 pak 이 바뀌면 configure_file 이 다시 돌고(재생성 의존성) MSBuild 가 갱신된
  # web.pak 입력을 추적해서 일어난다. OBJECT_DEPENDS 는 Visual Studio 생성기에서는 무시되고
  # Ninja·Makefile 생성기를 위한 것이다.
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/web.rc"
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/web.pak")
endif()
# exe 의 파일 속성(제품명·버전). 서명 서비스가 요구한다. 릴리스 빌드는 태그 버전을 넘긴다.
set(PULSE_VERSION "0.0.0" CACHE STRING "Version stored in pulse-engine.exe's file properties (x.y.z)")
if(NOT PULSE_VERSION MATCHES "^([0-9]+)\.([0-9]+)\.([0-9]+)$")
  message(FATAL_ERROR "PULSE_VERSION must look like 1.2.3, got: ${PULSE_VERSION}")
endif()
if(WIN32)
  set(PULSE_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(PULSE_VERSION_MINOR ${CMAKE_MATCH_2})
  set(PULSE_VERSION_PATCH ${CMAKE_MATCH_3})
  configure_file(resources/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
  list(APPEND PULSE_ENGINE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
add_executable(pulse-engine ${PULSE_ENGINE_SOURCES})
if(PULSE_WEB_PAK)
  target_include_directories(pulse-engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
target_link_libraries(pulse-engine PRIVATE pulse_core)

enable_testing()
add_subdirectory(tests)
