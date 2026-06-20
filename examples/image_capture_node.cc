#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/container/vector.h"

using namespace pangofly;

#define MAX_IMAGE_SIZE (640 * 480 * 3)

struct ImageFrame {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t stride;
    int32_t data_size;
    Vector<uint8_t> data;
};

struct FaceBox {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float score;
    int32_t id;
};

struct FaceLandmark {
    int32_t x;
    int32_t y;
};

struct FaceResult {
    int32_t frame_id;
    int64_t timestamp;
    int32_t face_count;
    float processing_time_ms;
    Vector<FaceBox> faces;
    Vector<FaceLandmark> landmarks;
};

class ImageCaptureNode {
public:
    ImageCaptureNode() : node_(nullptr), writer_() {}
    
    bool Init() {
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("image_capture_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        writer_ = node_->CreateWriter<ImageFrame>("image_channel");
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return false;
        }
        
        std::cout << "ImageCaptureNode initialized successfully" << std::endl;
        return true;
    }
    
    void Run(int frame_count = 100) {
        if (!writer_) {
            std::cerr << "Writer not initialized" << std::endl;
            return;
        }
        
        std::cout << "Starting image capture..." << std::endl;
        
        for (int i = 0; i < frame_count; ++i) {
            ImageFrame frame;
            
            frame.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            frame.width = 640;
            frame.height = 480;
            frame.format = 0;
            frame.stride = 640 * 3;
            frame.data_size = frame.width * frame.height * 3;
            
            frame.data.resize(frame.data_size);
            for (size_t j = 0; j < frame.data_size; j += 3) {
                frame.data[j] = static_cast<uint8_t>((i * 10 + j) % 256);
                frame.data[j + 1] = static_cast<uint8_t>((i * 5 + j * 2) % 256);
                frame.data[j + 2] = static_cast<uint8_t>((i * 3 + j * 3) % 256);
            }
            
            if (writer_->Write(frame)) {
                std::cout << "Frame " << i << " sent: " 
                          << frame.width << "x" << frame.height 
                          << ", size: " << frame.data_size << " bytes" << std::endl;
            } else {
                std::cerr << "Failed to write frame " << i << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Image capture completed" << std::endl;
    }
    
    void Shutdown() {
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    std::unique_ptr<Node> node_;
    std::shared_ptr<Writer<ImageFrame>> writer_;
};

int main(int argc, char** argv) {
    ImageCaptureNode node;
    
    if (!node.Init()) {
        return -1;
    }
    
    int frame_count = 100;
    if (argc > 1) {
        frame_count = std::stoi(argv[1]);
    }
    
    node.Run(frame_count);
    node.Shutdown();
    
    return 0;
}