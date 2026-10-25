cmake_minimum_required(VERSION 3.16)

project(shm_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PANGOFLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
include_directories(${PANGOFLY_ROOT})

add_executable(shm_example shm_example.cc)
target_link_libraries(shm_example pangofly pthread rt)

add_executable(shm_writer writer.cc)
target_link_libraries(shm_writer pangofly pthread rt)

add_executable(shm_reader reader.cc)
target_link_libraries(shm_reader pangofly pthread rt)
