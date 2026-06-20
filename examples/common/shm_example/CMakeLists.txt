# Common shm_example for all platforms
# This demonstrates basic pangofly reader/writer functionality

cmake_minimum_required(VERSION 3.16)

project(shm_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use parent directory's generated headers
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include_directories(${PARENT_DIR})

# Link to the built pangofly library
get_filename_component(PANGOFLY_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../build_luckfox ABSOLUTE)

# Multi-process example (contains fork)
add_executable(shm_example shm_example.cc)
target_link_libraries(shm_example pangofly pthread rt)
target_link_directories(shm_example PRIVATE ${PANGOFLY_LIB_DIR})

# Standalone reader
add_executable(shm_reader reader.cc)
target_link_libraries(shm_reader pangofly pthread rt)
target_link_directories(shm_reader PRIVATE ${PANGOFLY_LIB_DIR})

# Standalone writer
add_executable(shm_writer writer.cc)
target_link_libraries(shm_writer pangofly pthread rt)
target_link_directories(shm_writer PRIVATE ${PANGOFLY_LIB_DIR})
