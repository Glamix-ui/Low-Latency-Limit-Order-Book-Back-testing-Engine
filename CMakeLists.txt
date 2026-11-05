cmake_minimum_required(VERSION 3.16)
project(lob_project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Put the .exe/.dll/.so files in build/bin so Python can find them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Header-only core
add_library(lob INTERFACE)
target_include_directories(lob INTERFACE engine/include)

# Shared library for Python (lob_engine.dll on Windows, lob_engine.so on Linux)
add_library(lob_engine SHARED engine/src/engine_c_api.cpp)
target_link_libraries(lob_engine PUBLIC lob)
set_target_properties(lob_engine PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
if(MINGW)
  # Bundle the MinGW runtime into the DLL so Python can load it without PATH tricks.
  target_link_options(lob_engine PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit tests
add_executable(test_orderbook engine/tests/test_orderbook.cpp)
target_link_libraries(test_orderbook lob)
target_compile_options(test_orderbook PRIVATE -Wall -Wextra)
enable_testing()
add_test(NAME orderbook COMMAND test_orderbook)

# Optional: Google Benchmark suite (needs internet + git on first configure).
#   cmake .. -G Ninja -DLOB_BUILD_BENCHMARKS=ON
option(LOB_BUILD_BENCHMARKS "Build the Google Benchmark suite" OFF)
if(LOB_BUILD_BENCHMARKS)
  include(FetchContent)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3)
  FetchContent_MakeAvailable(googlebenchmark)
  add_executable(bench_orderbook engine/benchmarks/bench_orderbook.cpp)
  target_link_libraries(bench_orderbook lob benchmark::benchmark)
endif()
