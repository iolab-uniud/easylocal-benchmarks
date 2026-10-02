cmake_minimum_required(VERSION 3.16)

# EasyLocal 3 (v3.3.1) side of the EL3-versus-EL4 benchmark: a standalone
# project, built against an extracted EasyLocal 3 release.
project(easylocal_el3_comparison CXX)

set(EL3_SOURCE_DIR "" CACHE PATH "Path to the extracted EasyLocal 3 (v3.3.1) release")
if(NOT EL3_SOURCE_DIR OR NOT EXISTS "${EL3_SOURCE_DIR}/include/easylocal.hh")
    message(FATAL_ERROR "set EL3_SOURCE_DIR to the extracted EasyLocal 3 release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The headers used here never touch TBB; keep the build independent of it.
set(CMAKE_DISABLE_FIND_PACKAGE_TBB ON)
# Use Boost's own BoostConfig.cmake (FindBoost is removed in recent CMake).
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
# EasyLocal 3's own CMakeLists: INTERFACE target EasyLocal (include dir,
# Boost::program_options, Threads, C++17, -Wall).
add_subdirectory(${EL3_SOURCE_DIR} el3)

add_executable(el3_comparison driver.cc)
target_link_libraries(el3_comparison PRIVATE EasyLocal)
