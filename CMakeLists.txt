cmake_minimum_required(VERSION 3.15)
project(vpin-cpp-processor VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# SIMD support detection and flags
include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)

if(COMPILER_SUPPORTS_AVX2)
    message(STATUS "AVX2 support detected")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    add_definitions(-D__AVX2__)
endif()

if(COMPILER_SUPPORTS_AVX512)
    message(STATUS "AVX512 support detected")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f")
    add_definitions(-D__AVX512F__)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Find librdkafka
find_path(RDKAFKA_INCLUDE_DIR
    NAMES librdkafka/rdkafkacpp.h
    PATHS /usr/include /usr/local/include
)

find_library(RDKAFKA_LIBRARY
    NAMES rdkafka++
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

find_library(RDKAFKA_C_LIBRARY
    NAMES rdkafka
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(NOT RDKAFKA_INCLUDE_DIR OR NOT RDKAFKA_LIBRARY)
    message(FATAL_ERROR "librdkafka not found. Please install: sudo apt-get install librdkafka-dev")
endif()

message(STATUS "Found librdkafka: ${RDKAFKA_LIBRARY}")

# Find nlohmann/json
find_path(JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATHS /usr/include /usr/local/include
)

if(NOT JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json not found. Please install: sudo apt-get install nlohmann-json3-dev")
endif()

message(STATUS "Found nlohmann/json: ${JSON_INCLUDE_DIR}")

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${RDKAFKA_INCLUDE_DIR}
    ${JSON_INCLUDE_DIR}
)

# Source files
set(CORE_SOURCES
    src/core/vpin_engine.cpp
)

set(KAFKA_SOURCES
    src/kafka/kafka_consumer.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

# Main executable
add_executable(vpin-processor
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${KAFKA_SOURCES}
)

# Link libraries
target_link_libraries(vpin-processor
    ${RDKAFKA_LIBRARY}
    ${RDKAFKA_C_LIBRARY}
    Threads::Threads
    pthread
    dl
    rt
)

# Installation
install(TARGETS vpin-processor
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/vpin
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# Optional: Build tests
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== VPIN C++ Processor Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "AVX2 support: ${COMPILER_SUPPORTS_AVX2}")
message(STATUS "AVX512 support: ${COMPILER_SUPPORTS_AVX512}")
message(STATUS "librdkafka: ${RDKAFKA_LIBRARY}")
message(STATUS "nlohmann/json: ${JSON_INCLUDE_DIR}")
message(STATUS "========================================")
message(STATUS "")
