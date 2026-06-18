cmake_minimum_required(VERSION 3.16)

project(pangofly)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

# Pangofly platform selection
if(NOT DEFINED PANGOFLY_PLATFORM)
  set(PANGOFLY_PLATFORM "luckfox" CACHE STRING "Target platform: luckfox, k230, or posix")
endif()

message(STATUS "Building Pangofly for platform: ${PANGOFLY_PLATFORM}")

# Determine which transport to use based on platform
set(USE_K230_SHM OFF)
set(USE_POSIX_SHM OFF)
set(USE_LUCKFOX_SHM OFF)

if(PANGOFLY_PLATFORM STREQUAL "k230")
  set(USE_K230_SHM ON)
elseif(PANGOFLY_PLATFORM STREQUAL "luckfox")
  set(USE_POSIX_SHM ON)
else()
  set(USE_POSIX_SHM ON)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR})

# Source files
file(GLOB_RECURSE PANGOFLY_SRCS
  "${PROJECT_SOURCE_DIR}/pangofly/common/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/transport/shm/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/node/*.cc"
  "${PROJECT_SOURCE_DIR}/pangofly/*.cc"
)

# Filter sources based on platform
if(USE_K230_SHM)
  # K230 uses K230Shm
  list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*posix_segment\\.cc$")
elseif(USE_LUCKFOX_SHM)
  # Luckfox uses PosixSegment with luckfly optimizations
  list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*k230_shm\\.cc$")
else()
  # Default to PosixSegment
  list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*k230_shm\\.cc$")
endif()

# Exclude non-library files
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*nonpod_writer\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*nonpod_reader\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*writer\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*reader\\.cc$")
list(FILTER PANGOFLY_SRCS EXCLUDE REGEX ".*shm_example\\.cc$")

# Build shared library
add_library(pangofly SHARED ${PANGOFLY_SRCS})

target_link_libraries(pangofly pthread rt)

# Installation
install(TARGETS pangofly LIBRARY DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/pangofly/ DESTINATION include/pangofly FILES_MATCHING PATTERN "*.h")

# Build examples
option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
  # Basic test
  add_executable(pangofly_test
    ${PROJECT_SOURCE_DIR}/pangofly/examples/shm_example.cc
  )
  target_link_libraries(pangofly_test pangofly pthread rt)

  # Image capture node
  add_executable(image_capture_node
    ${PROJECT_SOURCE_DIR}/examples/image_capture_node.cc
  )
  target_link_libraries(image_capture_node pangofly pthread rt)

  # Face recognition node
  add_executable(face_recognition_node
    ${PROJECT_SOURCE_DIR}/examples/face_recognition_node.cc
  )
  target_link_libraries(face_recognition_node pangofly pthread rt)

  # Result display node
  add_executable(result_display_node
    ${PROJECT_SOURCE_DIR}/examples/result_display_node.cc
  )
  target_link_libraries(result_display_node pangofly pthread rt)

  # Camera capture node (requires RKADK - build with USE_RKADK=ON)
  if(USE_RKADK)
    add_executable(camera_capture_node
      ${PROJECT_SOURCE_DIR}/examples/camera_capture_node.cc
    )
    target_link_libraries(camera_capture_node pangofly pthread rt rockit rkaiq)
  endif()
endif()

message(STATUS "Pangofly build configuration:")
message(STATUS "  Platform: ${PANGOFLY_PLATFORM}")
message(STATUS "  K230 Shm: ${USE_K230_SHM}")
message(STATUS "  Posix Shm: ${USE_POSIX_SHM}")
message(STATUS "  RKADK Support: ${USE_RKADK}")