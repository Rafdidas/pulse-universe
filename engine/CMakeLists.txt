cmake_minimum_required(VERSION 3.25)
project(pulse_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
endif()

add_executable(pulse-engine src/main.cpp)
target_include_directories(pulse-engine PRIVATE src)

enable_testing()
add_subdirectory(tests)
