cmake_minimum_required(VERSION 3.16)

project(pangofly)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

include_directories(${PROJECT_SOURCE_DIR})

# v2 只需 core/shm 下的实现（地址层 + 分配层 + 消息层 + 传输层）
set(PANGOFLY_SRCS
  ${PROJECT_SOURCE_DIR}/core/shm/region.cc
  ${PROJECT_SOURCE_DIR}/core/shm/segment.cc
  ${PROJECT_SOURCE_DIR}/core/pangofly.cc
)

add_library(pangofly SHARED ${PANGOFLY_SRCS})
target_link_libraries(pangofly pthread rt)

# ------------------------------------------------------------
# IDL 编译器 idc
# ------------------------------------------------------------
add_executable(idc
  ${PROJECT_SOURCE_DIR}/idl/tools/idc.cc
  ${PROJECT_SOURCE_DIR}/idl/lexer/lexer.cc
  ${PROJECT_SOURCE_DIR}/idl/parser/parser.cc
  ${PROJECT_SOURCE_DIR}/idl/codegen/code_generator.cc
)
target_include_directories(idc PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/idl)

# ------------------------------------------------------------
# 示例
# ------------------------------------------------------------
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
  add_subdirectory(${PROJECT_SOURCE_DIR}/examples/common/shm_example)
endif()

install(TARGETS pangofly LIBRARY DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/core/ DESTINATION include/pangofly
        FILES_MATCHING PATTERN "*.h")

message(STATUS "Pangofly v2 build:")
message(STATUS "  sources: ${PANGOFLY_SRCS}")
