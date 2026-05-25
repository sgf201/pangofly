// Pangofly Gesture Subscriber
// Receives images, runs gesture recognition, and publishes results

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "pangofly/pangofly.h"
#include "idl/container/vector.h"

// Define our message structures
struct HandKeypoint {
    float x;
    float y;
    float score;
};

struct HandDetectionResult {
    float score;
    float x1;
    float y1;
    float x2;
    float y2;
    pangofly::Vector<HandKeypoint> keypoints;
};

struct GestureResult {
    uint64_t timestamp;
    int32_t gesture_class;
    float confidence;
    pangofly::Vector<HandDetectionResult> hands;
};

struct ImageFrame {
    uint64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    uint64_t data_size;
    pangofly::Vector<uint8_t> data;
};

std::atomic<bool> running(true);

// Simulated gesture recognition
// In a real implementation, this would use the KPU models from the gesture demo
bool RecognizeGestureMock(const uint8_t* image_data, int width, int height, 
                          GestureResult* result) {
    static int gesture_index = 0;
    
    result->timestamp = gesture_index++;
    result->gesture_class = gesture_index % 10; // 0-9 different gestures
    result->confidence = 0.85f + (gesture_index % 15) * 0.01f;
    
    // Add one dummy hand detection
    HandDetectionResult hand;
    hand.score = 0.92f;
    hand.x1 = 100.0f;
    hand.y1 = 100.0f;
    hand.x2 = 300.0f;
    hand.y2 = 400.0f;
    
    // Add 21 hand keypoints
    for (int i = 0; i < 21; ++i) {
        HandKeypoint kp;
        kp.x = 150.0f + i * 7.0f;
        kp.y = 180.0f + i * 5.0f;
        kp.score = 0.9f - i * 0.02f;
        hand.keypoints.push_back(kp);
    }
    
    result->hands.push_back(hand);
    
    return true;
}

void SubscriberMain() {
    std::cout << "Pangofly Gesture Subscriber starting..." << std::endl;
    
    // Initialize Pangofly
    if (!pangofly::Init()) {
        std::cerr << "Failed to initialize Pangofly" << std::endl;
        return;
    }
    
    // Create node, reader for images, and writer for results
    auto node = pangofly::CreateNode("gesture_subscriber");
    auto image_reader = node->CreateReader<ImageFrame>("gesture_channel");
    auto result_writer = node->CreateWriter<GestureResult>("gesture_result_channel");
    
    std::cout << "Subscriber node created. Waiting for images..." << std::endl;
    
    ImageFrame frame;
    pangofly::MessageInfo info;
    uint64_t processed_count = 0;
    
    while (running) {
        // Read image from Pangofly
        bool success = image_reader->Read(&frame, &info);
        if (success) {
            if (processed_count % 30 == 0) {
                std::cout << "Received frame seq=" << info.seq
                          << ", timestamp=" << frame.timestamp
                          << ", size=" << frame.data_size << std::endl;
            }
            
            // Run gesture recognition
            GestureResult result;
            if (RecognizeGestureMock(frame.data.data(), frame.width, frame.height, &result)) {
                if (processed_count % 30 == 0) {
                    std::cout << "  Recognized gesture=" << result.gesture_class
                              << ", confidence=" << result.confidence
                              << ", hands=" << result.hands.size() << std::endl;
                }
                
                // Publish the result
                result_writer->Write(result);
            }
            
            processed_count++;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    std::cout << "Subscriber shutting down..." << std::endl;
    pangofly::Shutdown();
}

int main(int argc, char* argv[]) {
    std::cout << "Pangofly Gesture Subscriber" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // Run subscriber
    SubscriberMain();
    
    return 0;
}
