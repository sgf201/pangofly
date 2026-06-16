#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/container/vector.h"

using namespace pangofly;
using namespace pangofly::examples;

class ImageCaptureNode {
public:
    ImageCaptureNode() : node_(nullptr), writer_(nullptr) {}
    
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
            auto frame = CreateSimulatedFrame(i);
            
            if (writer_->Write(frame)) {
                std::cout << "Frame " << i << " sent: " 
                          << frame.width() << "x" << frame.height() 
                          << ", size: " << frame.data().size() << " bytes" << std::endl;
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
    ImageFrame CreateSimulatedFrame(int frame_id) {
        ImageFrame frame;
        
        frame.set_timestamp(std::chrono::system_clock::now().time_since_epoch().count());
        frame.set_width(640);
        frame.set_height(480);
        frame.set_format(0); // BGR format
        frame.set_stride(640 * 3);
        
        size_t data_size = frame.width() * frame.height() * 3;
        std::vector<uint8_t> data(data_size);
        
        for (size_t j = 0; j < data_size; j += 3) {
            data[j] = (frame_id * 10 + j) % 256;         // B
            data[j + 1] = (frame_id * 5 + j * 2) % 256;   // G
            data[j + 2] = (frame_id * 3 + j * 3) % 256;   // R
        }
        
        frame.set_data(std::move(data));
        
        return frame;
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
