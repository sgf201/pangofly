# Face recognition demo for Luckfox platform
# Contains: image capture, face recognition, result display, and V4L2 camera capture

cmake_minimum_required(VERSION 3.16)

project(face_recognition_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use parent directory's generated headers
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include_directories(${PARENT_DIR})

# Include IDL generated headers (same directory)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Include pangofly headers (note: code uses "core/pangofly.h" so root dir is needed)
get_filename_component(PANGOFLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
include_directories(${PANGOFLY_ROOT})

# Link to the built pangofly library
link_directories(${PANGOFLY_ROOT}/build_luckfox)

# RKNN and OpenCV paths
set(OPENCV_DIR /home/sgf/ws/luckfox_pangofly/luckfox_pico_rkmpi_example/lib/uclibc)
set(OpenCV_DIR ${OPENCV_DIR}/lib/cmake/opencv4)
find_package(OpenCV REQUIRED)
include_directories(/home/sgf/ws/luckfox_pangofly/luckfox_pico_rkmpi_example/include)

# RKNN include and library directory
include_directories(/home/sgf/ws/luckfox_pangofly/luckfox_pico_rkmpi_example/include/rknn)
link_directories(/home/sgf/ws/luckfox_pangofly/luckfox_pico_rkmpi_example/lib/uclibc)

# ============================================
# Node 1: Image Capture (simulated frames)
# ============================================
add_executable(image_capture_node image_capture_node.cc)
target_link_libraries(image_capture_node pangofly pthread rt)

# ============================================
# Node 2: Face Recognition (REAL mode with RetinaFace)
# ============================================
add_executable(face_recognition_node face_recognition_node.cc)
target_link_libraries(face_recognition_node 
    pangofly 
    ${OpenCV_LIBS}
    rknnmrt
    pthread 
    rt)

# Copy model files to build directory
add_custom_command(TARGET face_recognition_node POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    $<TARGET_FILE_DIR:face_recognition_node>/model)

# ============================================
# Node 3: Result Display
# ============================================
add_executable(result_display_node result_display_node.cc)
target_link_libraries(result_display_node pangofly pthread rt)

# ============================================
# Tool: V4L2 Camera Capture (standalone test)
# ============================================
add_executable(v4l2_camera_capture v4l2_camera_capture.cc)
target_link_libraries(v4l2_camera_capture pthread rt)