# Face recognition demo for Luckfox platform
# Contains: image capture, face recognition, result display, and V4L2 camera capture

cmake_minimum_required(VERSION 3.16)

project(face_recognition_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================
# Step 1: Build IDL compiler (idc) with host compiler at configure time
# ============================================
get_filename_component(PANGOFLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

set(IDC_SOURCES
    ${PANGOFLY_ROOT}/idl/tools/idc.cc
    ${PANGOFLY_ROOT}/idl/lexer/lexer.cc
    ${PANGOFLY_ROOT}/idl/parser/parser.cc
    ${PANGOFLY_ROOT}/idl/codegen/code_generator.cc
)

set(IDC_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/idc)

# Build idc with host compiler
if(CMAKE_CROSSCOMPILING)
    find_program(HOST_CXX_COMPILER g++ REQUIRED)
else()
    set(HOST_CXX_COMPILER ${CMAKE_CXX_COMPILER})
endif()

execute_process(
    COMMAND ${HOST_CXX_COMPILER} -std=c++17
        -I${PANGOFLY_ROOT} -I${PANGOFLY_ROOT}/idl
        -o ${IDC_EXECUTABLE}
        ${IDC_SOURCES}
    RESULT_VARIABLE IDC_BUILD_RESULT
    OUTPUT_VARIABLE IDC_BUILD_OUTPUT
    ERROR_VARIABLE IDC_BUILD_ERROR
)

if(NOT IDC_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build IDL compiler:\n${IDC_BUILD_ERROR}")
endif()

message(STATUS "IDL compiler built: ${IDC_EXECUTABLE}")

# ============================================
# Step 2: Generate face_detection.h from IDL
# ============================================
set(IDL_FILE ${PANGOFLY_ROOT}/examples/face_detection.idl)
set(GENERATED_HEADER ${CMAKE_CURRENT_BINARY_DIR}/face_detection.h)

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${IDC_EXECUTABLE} -o ${GENERATED_HEADER} ${IDL_FILE}
    DEPENDS ${IDL_FILE} ${IDC_SOURCES}
    COMMENT "Generating face_detection.h from IDL"
    VERBATIM
)

add_custom_target(generate_face_detection_header DEPENDS ${GENERATED_HEADER})

# Include generated header directory
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# ============================================
# Include directories
# ============================================
# Use parent directory's generated headers
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include_directories(${PARENT_DIR})

# Include pangofly headers (note: code uses "core/pangofly.h" so root dir is needed)
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

# RGA include and library directory (for hardware-accelerated NV12->RGB conversion)
set(RGA_DIR /home/sgf/ws/luckfox_pangofly/media/rga/release_rga_rv1106_arm-rockchip830-linux-uclibcgnueabihf)
include_directories(${RGA_DIR}/include)
link_directories(${RGA_DIR}/lib)

# ============================================
# Node 1: Image Capture (camera + RGA acceleration)
# ============================================
add_executable(image_capture_node image_capture_node.cc)
add_dependencies(image_capture_node generate_face_detection_header)
target_link_libraries(image_capture_node pangofly rga pthread rt)

# ============================================
# Node 2: Face Recognition (REAL mode with RetinaFace)
# ============================================
add_executable(face_recognition_node face_recognition_node.cc)
add_dependencies(face_recognition_node generate_face_detection_header)
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
add_dependencies(result_display_node generate_face_detection_header)
target_link_libraries(result_display_node pangofly pthread rt)

# ============================================
# Tool: V4L2 Camera Capture (standalone test)
# ============================================
add_executable(v4l2_camera_capture v4l2_camera_capture.cc)
target_link_libraries(v4l2_camera_capture pthread rt)