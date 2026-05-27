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

# For build_k230, we use PosixSegment instead of K230Shm
# because lwp_shmget syscall may not work on all K230 builds
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*k230_shm\\.cc$")

list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*nonpod_writer\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*nonpod_reader\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*writer\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*reader\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*shm_example\\.cc$")

add_library(pangofly SHARED ${PANGOFLY_SRCS})

target_link_libraries(pangofly pthread rt)

add_executable(pangofly_test
  ${PROJECT_SOURCE_DIR}/pangofly/examples/shm_example.cc
)

target_link_libraries(pangofly_test pangofly pthread rt)

add_executable(pangofly_nonpod_test
  ${PROJECT_SOURCE_DIR}/examples/nonpod_shm_example.cc
)

target_include_directories(pangofly_nonpod_test PRIVATE
  ${PROJECT_SOURCE_DIR}/idl
  ${PROJECT_SOURCE_DIR}/idl/container
)

target_link_libraries(pangofly_nonpod_test pangofly pthread rt)