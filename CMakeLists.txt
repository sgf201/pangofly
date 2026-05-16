cmake_minimum_required(VERSION 3.16)

project(pangofly)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

include_directories(${PROJECT_SOURCE_DIR})

file(GLOB_RECURSE PANGOFLY_SRCS
  "${PROJECT_SOURCE_DIR}/pangofly/common/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/transport/shm/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/node/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/*.cc"
)

add_library(pangofly SHARED ${PANGOFLY_SRCS})

target_link_libraries(pangofly pthread rt)

add_executable(shm_test
  ${PROJECT_SOURCE_DIR}/design/test/test.cpp
)

target_link_libraries(shm_test rt)

add_executable(pangofly_test
  ${PROJECT_SOURCE_DIR}/pangofly/examples/shm_example.cc
)

target_link_libraries(pangofly_test pangofly pthread rt)