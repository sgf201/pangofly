#include <iostream>
#include <memory>
#include <chrono>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/container/vector.h"

using namespace pangofly;
using namespace pangofly::examples;

class ResultDisplayNode {
public:
    ResultDisplayNode() : node_(nullptr), reader_(nullptr) {}
    
    bool Init() {
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("result_display_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        reader_ = node_->CreateReader<FaceResult>("face_result_channel", 
            std::bind(&ResultDisplayNode::OnResultReceived, this, std::placeholders::_1));
        if (!reader_) {
            std::cerr << "Failed to create reader" << std::endl;
            return false;
        }
        
        std::cout << "ResultDisplayNode initialized successfully" << std::endl;
        return true;
    }
    
    void Run() {
        std::cout << "Starting result display..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        while (running_) {
            node_->Observe();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "\nResult display stopped" << std::endl;
        PrintStatistics();
    }
    
    void Stop() {
        running_ = false;
    }
    
    void Shutdown() {
        Stop();
        reader_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    void OnResultReceived(const FaceResult& result) {
        total_frames_++;
        total_faces_ += result.face_count();
        total_processing_time_ += result.processing_time_ms();
        
        std::cout << "\n==========================================" << std::endl;
        std::cout << "Frame ID: " << result.frame_id() << std::endl;
        std::cout << "Timestamp: " << result.timestamp() << std::endl;
        std::cout << "Processing Time: " << result.processing_time_ms() << "ms" << std::endl;
        std::cout << "Faces Detected: " << result.face_count() << std::endl;
        
        if (result.face_count() > 0) {
            std::cout << "------------------------------------------" << std::endl;
            std::cout << "Face Results:" << std::endl;
            
            for (const auto& face : result.faces()) {
                std::cout << "\nFace ID: " << face.id() << std::endl;
                std::cout << "  Confidence: " << (face.score() * 100) << "%" << std::endl;
                std::cout << "  Bounding Box: (" << face.x() << ", " << face.y() << ") "
                          << "Width: " << face.width() << ", Height: " << face.height() << std::endl;
            }
            
            std::cout << "\nLandmarks Count: " << result.landmarks().size() << std::endl;
        }
        
        std::cout << "==========================================" << std::endl;
    }
    
    void PrintStatistics() {
        std::cout << "\n==========================================" << std::endl;
        std::cout << "          Statistics Summary" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Total Frames Processed: " << total_frames_ << std::endl;
        std::cout << "Total Faces Detected: " << total_faces_ << std::endl;
        
        if (total_frames_ > 0) {
            std::cout << "Average Faces per Frame: " 
                      << static_cast<float>(total_faces_) / total_frames_ << std::endl;
            std::cout << "Average Processing Time: " 
                      << total_processing_time_ / total_frames_ << "ms" << std::endl;
        }
        
        std::cout << "==========================================" << std::endl;
    }
    
private:
    std::unique_ptr<Node> node_;
    std::shared_ptr<Reader<FaceResult>> reader_;
    bool running_ = true;
    
    // Statistics
    int total_frames_ = 0;
    int total_faces_ = 0;
    float total_processing_time_ = 0.0f;
};

int main(int argc, char** argv) {
    ResultDisplayNode node;
    
    if (!node.Init()) {
        return -1;
    }
    
    node.Run();
    node.Shutdown();
    
    return 0;
}
