// Pangofly Gesture Publisher
// Reads camera images and publishes via Pangofly shared memory

#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <memory>

#include "pangofly/pangofly.h"
#include "idl/container/vector.h"

// Define our message structures
struct ImageFrame {
    uint64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;  // 0: NV12, 1: RGB
    uint64_t data_size;
    pangofly::Vector<uint8_t> data;
};

std::atomic<bool> running(true);

// Simulated or actual V4L2 image capture
// In a real implementation, this would use the same approach as camera_rtsp_demo
bool CaptureImageMock(uint8_t** data, uint64_t* size, int* width, int* height) {
    static uint8_t* dummy_buffer = nullptr;
    static bool initialized = false;
    
    if (!initialized) {
        *width = 640;
        *height = 480;
        *size = (*width) * (*height) * 3 / 2; // NV12 format
        dummy_buffer = new uint8_t[*size];
        
        // Initialize with a test pattern
        for (int i = 0; i < *width * *height; ++i) {
            dummy_buffer[i] = 128 + (i % 256); // Y plane
        }
        for (uint64_t i = *width * *height; i < *size; ++i) {
            dummy_buffer[i] = 128; // UV plane
        }
        
        initialized = true;
    }
    
    *data = dummy_buffer;
    return true;
}

void PublisherMain() {
    std::cout << "Pangofly Gesture Publisher starting..." << std::endl;
    
    // Initialize Pangofly
    if (!pangofly::Init()) {
        std::cerr << "Failed to initialize Pangofly" << std::endl;
        return;
    }
    
    // Create node and writer
    auto node = pangofly::CreateNode("gesture_publisher");
    auto writer = node->CreateWriter<ImageFrame>("gesture_channel");
    
    std::cout << "Publisher node created. Starting image capture..." << std::endl;
    
    int width, height;
    uint8_t* image_data;
    uint64_t data_size;
    uint64_t frame_count = 0;
    
    while (running) {
        // Capture image
        if (CaptureImageMock(&image_data, &data_size, &width, &height)) {
            // Prepare message
            ImageFrame frame;
            frame.timestamp = frame_count++;
            frame.width = width;
            frame.height = height;
            frame.format = 0; // NV12
            frame.data_size = data_size;
            
            // Copy image data
            frame.data.reserve(data_size);
            for (uint64_t i = 0; i < data_size; ++i) {
                frame.data.push_back(image_data[i]);
            }
            
            // Write to Pangofly
            bool success = writer->Write(frame);
            if (success && frame_count % 30 == 0) {
                std::cout << "Published frame " << frame_count 
                          << ", size=" << data_size << std::endl;
            }
        }
        
        // Control frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
    }
    
    std::cout << "Publisher shutting down..." << std::endl;
    pangofly::Shutdown();
}

int main(int argc, char* argv[]) {
    std::cout << "Pangofly Gesture Publisher" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // Run publisher
    PublisherMain();
    
    return 0;
}
