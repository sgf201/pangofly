cmake_minimum_required(VERSION 3.16)

project(pangofly_k230)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

include_directories(${PROJECT_SOURCE_DIR}/..)
include_directories(/home/sgf/ws/src/rtsmart/rtsmart/kernel/rt-thread/components/lwp)
include_directories(/home/sgf/ws/src/rtsmart/rtsmart/kernel/rt-thread/include)

file(GLOB_RECURSE PANGOFLY_SRCS
  "${PROJECT_SOURCE_DIR}/../pangofly/common/*.cc"
  "${PROJECT_SOURCE_DIR}/../pangofly/transport/shm/*.cc"
  "${PROJECT_SOURCE_DIR}/../pangofly/node/*.cc"
  "${PROJECT_SOURCE_DIR}/../pangofly/*.cc"
  "${PROJECT_SOURCE_DIR}/*.cc"
)

add_library(pangofly_k230 SHARED ${PANGOFLY_SRCS})

target_link_libraries(pangofly_k230 pthread)

add_executable(pangofly_k230_test
  ${PROJECT_SOURCE_DIR}/../pangofly/examples/shm_example.cc
)

target_link_libraries(pangofly_k230_test pangofly_k230 pthread)