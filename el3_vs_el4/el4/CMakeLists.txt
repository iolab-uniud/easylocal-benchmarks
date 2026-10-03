cmake_minimum_required(VERSION 3.25)

# EasyLocal 4 side of the EL3-versus-EL4 benchmark: the examples' models,
# compiled from an EasyLocal checkout (header-only Core, no other dependency),
# and the deltas of ../../common that the examples do not have.
project(easylocal_el4_comparison LANGUAGES CXX)

set(EASYLOCAL_SOURCE_DIR "" CACHE PATH "Path to an EasyLocal 4 checkout")
if(NOT EXISTS "${EASYLOCAL_SOURCE_DIR}/include/easylocal/easylocal.hpp")
    message(FATAL_ERROR "EASYLOCAL_SOURCE_DIR must point to an EasyLocal 4 checkout")
endif()

add_executable(el4_comparison driver.cpp)
target_compile_features(el4_comparison PRIVATE cxx_std_23)
set_target_properties(el4_comparison PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(
    el4_comparison
    PRIVATE
        "${EASYLOCAL_SOURCE_DIR}/include"
        "${EASYLOCAL_SOURCE_DIR}/examples"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../common"
)
