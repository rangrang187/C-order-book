cmake_minimum_required(VERSION 3.20)

project(low_latency
    VERSION 0.1.0
    DESCRIPTION "A C++20 low-latency project"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(low_latency
    src/main.cpp
)

target_compile_features(low_latency PRIVATE cxx_std_20)

target_compile_options(low_latency PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

enable_testing()
add_test(NAME low_latency_runs COMMAND low_latency)
