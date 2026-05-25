# Pangofly Gesture Application

This is a gesture recognition application using Pangofly for inter-process communication.

## Components

- **gesture_publisher**: Captures camera images and publishes them via Pangofly shared memory
- **gesture_subscriber**: Receives images, runs gesture recognition using KPU, and publishes results

## Building

For x86 (Linux):
```bash
cd gesture
make -f Makefile
```

For K230 platform:
```bash
# Use the buildroot package in k230_linux_pangofly repository
```

## Running

### On K230 Linux

```bash
# In terminal 1 - Run publisher
LD_LIBRARY_PATH=/usr/lib ./gesture_publisher &

# In terminal 2 - Run subscriber
LD_LIBRARY_PATH=/usr/lib ./gesture_subscriber
```

## Architecture

The application uses Pangofly shared memory communication to:
1. Publisher captures NV12 images from camera and sends them
2. Subscriber receives images, runs hand detection and keypoint recognition
3. Results are published for other applications to use

## Integration with Pangofly

This application demonstrates how to:
- Use Pangofly's CreateNode, CreateWriter, CreateReader APIs
- Define custom message structures
- Handle large image data efficiently

## Next Steps

- Integrate real V4L2 camera capture from camera_rtsp_demo
- Add actual KPU inference from the gesture recognition demo
- Add shared memory support for large images to avoid copying
