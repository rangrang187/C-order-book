cmake_minimum_required(VERSION 3.20)

project(low_latency_order_book
    VERSION 0.1.0
    DESCRIPTION "A correctness-first C++20 limit order book"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(enable_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(order_book
    src/order_book.cpp
)

target_include_directories(order_book
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(order_book PUBLIC cxx_std_20)
enable_project_warnings(order_book)

add_executable(order_book_demo
    src/main.cpp
)

target_link_libraries(order_book_demo PRIVATE order_book)
enable_project_warnings(order_book_demo)

include(CTest)

if(BUILD_TESTING)
    add_executable(order_book_tests
        tests/order_book_tests.cpp
    )

    target_link_libraries(order_book_tests PRIVATE order_book)
    enable_project_warnings(order_book_tests)

    add_test(NAME order_book_tests COMMAND order_book_tests)
endif()
