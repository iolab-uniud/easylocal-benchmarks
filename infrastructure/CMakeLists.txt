cmake_minimum_required(VERSION 3.25)

# EasyLocal infrastructure benchmarks: neighborhood traversal, runner-level
# search and tracing overhead, compiled from an EasyLocal checkout (header-only
# Core and the examples' models, plus the deltas of ../common that the examples
# do not have).
project(easylocal_infrastructure_benchmarks LANGUAGES CXX)

set(EASYLOCAL_SOURCE_DIR "" CACHE PATH "Path to an EasyLocal 4 checkout")
if(NOT EXISTS "${EASYLOCAL_SOURCE_DIR}/include/easylocal/easylocal.hpp")
    message(FATAL_ERROR "EASYLOCAL_SOURCE_DIR must point to an EasyLocal 4 checkout")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

function(easylocal_benchmark target source)
    add_executable(${target} ${source})
    target_include_directories(
        ${target}
        PRIVATE
            "${EASYLOCAL_SOURCE_DIR}/include"
            "${EASYLOCAL_SOURCE_DIR}/examples"
            "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    )
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

easylocal_benchmark(easylocal_neighborhood_traversal_benchmark traversal_benchmark.cpp)
easylocal_benchmark(easylocal_neighborhood_search_benchmark search_benchmark.cpp)
easylocal_benchmark(easylocal_trace_benchmark trace_benchmark.cpp)
easylocal_benchmark(easylocal_trace_cost_encoding_benchmark trace_cost_encoding_benchmark.cpp)
