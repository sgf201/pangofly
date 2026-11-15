cmake_minimum_required(VERSION 3.16)

project(shm_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PANGOFLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
include_directories(${PANGOFLY_ROOT})

find_package(Threads REQUIRED)

add_executable(shm_example shm_example.cc)
target_link_libraries(shm_example pangofly pthread rt)

add_executable(shm_writer writer.cc)
target_link_libraries(shm_writer pangofly pthread rt)

add_executable(shm_reader reader.cc)
target_link_libraries(shm_reader pangofly pthread rt)

add_executable(shm_thread_pipeline thread_pipeline.cc)
target_link_libraries(shm_thread_pipeline pangofly Threads::Threads rt)

# 崩溃恢复 + 类型校验的多进程自测用例（配合 scripts/p0_check.sh 使用）
add_executable(shm_p0_check p0_check.cc)
target_link_libraries(shm_p0_check pangofly pthread rt)

# 协程机制自测（上下文切换 / 事件唤醒 / Runtime 订阅）
add_executable(shm_coro_check coro_check.cc)
target_link_libraries(shm_coro_check pangofly pthread rt)
