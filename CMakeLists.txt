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

# Face Recognition Demo Nodes
add_executable(image_capture_node
  ${PROJECT_SOURCE_DIR}/examples/image_capture_node.cc
)

target_include_directories(image_capture_node PRIVATE
  ${PROJECT_SOURCE_DIR}/idl
  ${PROJECT_SOURCE_DIR}/idl/container
)

target_link_libraries(image_capture_node pangofly pthread rt)

add_executable(face_recognition_node
  ${PROJECT_SOURCE_DIR}/examples/face_recognition_node.cc
)

target_include_directories(face_recognition_node PRIVATE
  ${PROJECT_SOURCE_DIR}/idl
  ${PROJECT_SOURCE_DIR}/idl/container
)

target_link_libraries(face_recognition_node pangofly pthread rt)

add_executable(result_display_node
  ${PROJECT_SOURCE_DIR}/examples/result_display_node.cc
)

target_include_directories(result_display_node PRIVATE
  ${PROJECT_SOURCE_DIR}/idl
  ${PROJECT_SOURCE_DIR}/idl/container
)

target_link_libraries(result_display_node pangofly pthread rt)