# Face Recognition Demo with Pangofly

This demo demonstrates how to use Pangofly shared memory for inter-process communication in a face recognition pipeline.

## Architecture

```
┌─────────────────┐      image_channel       ┌─────────────────────┐      face_result_channel       ┌─────────────────┐
│ ImageCapture    │ ──────────────────────→  │ FaceRecognition     │ ──────────────────────────→  │ ResultDisplay   │
│     Node        │   ImageFrame             │       Node          │   FaceResult                 │     Node        │
│  (图像采集)     │                          │   (人脸检测处理)    │                            │  (结果显示)     │
└─────────────────┘                          └─────────────────────┘                            └─────────────────┘
```

## Nodes

### 1. ImageCaptureNode (Simulated)
- Generates simulated image frames
- Usage: `./image_capture_node [frame_count]`
- Default: 100 frames

### 2. CameraCaptureNode (Real Camera)
- Captures frames from real camera using RKADK framework
- Usage: `./camera_capture_node [camera_id] [frame_count]`
- Requires RKADK libraries

### 3. FaceRecognitionNode
- Receives image frames
- Performs face detection (currently simulated)
- Sends results to display node
- Usage: `./face_recognition_node`

### 4. ResultDisplayNode
- Receives face detection results
- Displays results and statistics
- Usage: `./result_display_node`

## Building

### Standard Build (Simulated Images Only)

```bash
cd pangofly
mkdir build && cd build
cmake ..
make
```

### Build with RKADK Support (Real Camera)

```bash
cd pangofly
mkdir build && cd build
cmake -DUSE_RKADK=ON -DRKADK_PATH=/path/to/luckfox_pangofly ..
make
```

**Note**: RKADK build requires cross-compilation toolchain for RV1106.

## Running

### Using Simulated Images

```bash
# Terminal 1: Start face recognition node
./face_recognition_node &

# Terminal 2: Start result display node
./result_display_node &

# Terminal 3: Send simulated images
./image_capture_node 100
```

### Using Real Camera (on target board)

```bash
# Terminal 1: Start face recognition node
./face_recognition_node &

# Terminal 2: Start result display node
./result_display_node &

# Terminal 3: Capture from camera
./camera_capture_node 0 -1  # Camera 0, run until Ctrl+C
```

## Data Structures

### ImageFrame
```idl
struct ImageFrame {
    int64_t timestamp;      // Frame timestamp
    int32_t width;          // Image width
    int32_t height;         // Image height
    int32_t format;         // Pixel format (RK_FMT_YUV420SP, etc.)
    int32_t stride;         // Line stride
    vector<uint8> data;     // Image data
};
```

### FaceResult
```idl
struct FaceResult {
    int32_t frame_id;       // Frame sequence number
    int64_t timestamp;      // Detection timestamp
    int32_t face_count;     // Number of faces detected
    vector<FaceBox> faces;  // Face bounding boxes
    vector<FaceLandmark> landmarks;  // Face landmarks
    float processing_time_ms;  // Processing time
};
```

## Integration with RockIVA

To use real face detection, integrate with RockIVA SDK:

```cpp
#include "rockiva_face_api.h"

// Initialize RockIVA
RockIvaHandle handle;
RockIvaFaceTaskParams params;
// ... configure params ...
RockIva_Init(&handle, &params);

// Process frame
RockIvaFaceDetResult result;
RockIva_ProcessFrame(handle, &image_frame, &result);

// Convert to Pangofly FaceResult
FaceResult face_result;
// ... convert result ...
```

## Performance

- Shared memory communication: ~0.1ms latency
- Zero-copy data transfer
- Suitable for real-time video processing

## Notes

1. Ensure Pangofly kernel module is loaded with correct reserved region:
   ```bash
   cat /sys/module/pangofly_mmap/parameters/reserve_addr
   cat /sys/module/pangofly_mmap/parameters/reserve_size
   ```

2. For cross-compilation, use the toolchain from luckfox_pangofly:
   ```bash
   source /path/to/luckfox_pangofly/envsetup.sh
   ```

3. IQ files are required for ISP initialization:
   - Default path: `/etc/iqfiles`
   - Copy from luckfox_pangofly project