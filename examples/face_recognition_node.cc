#include <iostream>
#include <memory>
#include <chrono>
#include <random>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/container/vector.h"

using namespace pangofly;
using namespace pangofly::examples;

class FaceRecognitionNode {
public:
    FaceRecognitionNode() : node_(nullptr), reader_(nullptr), writer_(nullptr) {}
    
    bool Init() {
        if (!pangofly::Init()) {
            std::cerr << "Failed to initialize pangofly" << std::endl;
            return false;
        }
        
        node_ = CreateNode("face_recognition_node");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return false;
        }
        
        reader_ = node_->CreateReader<ImageFrame>("image_channel", 
            std::bind(&FaceRecognitionNode::OnImageReceived, this, std::placeholders::_1));
        if (!reader_) {
            std::cerr << "Failed to create reader" << std::endl;
            return false;
        }
        
        writer_ = node_->CreateWriter<FaceResult>("face_result_channel");
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return false;
        }
        
        std::cout << "FaceRecognitionNode initialized successfully" << std::endl;
        return true;
    }
    
    void Run() {
        std::cout << "Starting face recognition..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        while (running_) {
            node_->Observe();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "Face recognition stopped" << std::endl;
    }
    
    void Stop() {
        running_ = false;
    }
    
    void Shutdown() {
        Stop();
        reader_.reset();
        writer_.reset();
        node_.reset();
        pangofly::Shutdown();
    }

private:
    void OnImageReceived(const ImageFrame& frame) {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::cout << "\nReceived frame: " 
                  << frame.width() << "x" << frame.height() 
                  << ", timestamp: " << frame.timestamp() << std::endl;
        
        FaceResult result = PerformFaceDetection(frame);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        result.set_processing_time_ms(static_cast<float>(duration.count()));
        result.set_timestamp(frame.timestamp());
        
        if (writer_->Write(result)) {
            std::cout << "Face detection completed in " << duration.count() << "ms" << std::endl;
            std::cout << "Detected " << result.face_count() << " faces" << std::endl;
            
            for (const auto& face : result.faces()) {
                std::cout << "  Face ID: " << face.id() 
                          << ", Score: " << face.score()
                          << ", Box: (" << face.x() << "," << face.y() << ")-(" 
                          << face.x() + face.width() << "," << face.y() + face.height() << ")" << std::endl;
            }
        } else {
            std::cerr << "Failed to send face result" << std::endl;
        }
    }
    
    FaceResult PerformFaceDetection(const ImageFrame& frame) {
        FaceResult result;
        
        result.set_frame_id(frame_id_++);
        result.set_face_count(simulate_random_faces());
        
        for (int i = 0; i < result.face_count(); ++i) {
            FaceBox face;
            face.set_id(i + 1);
            face.set_score(static_cast<float>(0.8 + (std::rand() % 20) / 100.0));
            
            int margin = 50;
            face.set_x(margin + std::rand() % (frame.width() - margin * 2 - 100));
            face.set_y(margin + std::rand() % (frame.height() - margin * 2 - 100));
            face.set_width(60 + std::rand() % 80);
            face.set_height(60 + std::rand() % 80);
            
            result.mutable_faces()->push_back(face);
            
            for (int j = 0; j < 5; ++j) {
                FaceLandmark landmark;
                landmark.set_x(face.x() + std::rand() % face.width());
                landmark.set_y(face.y() + std::rand() % face.height());
                result.mutable_landmarks()->push_back(landmark);
            }
        }
        
        return result;
    }
    
    int simulate_random_faces() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dist(0, 3);
        return dist(gen);
    }
    
private:
    std::unique_ptr<Node> node_;
    std::shared_ptr<Reader<ImageFrame>> reader_;
    std::shared_ptr<Writer<FaceResult>> writer_;
    bool running_ = true;
    int frame_id_ = 0;
};

int main(int argc, char** argv) {
    FaceRecognitionNode node;
    
    if (!node.Init()) {
        return -1;
    }
    
    node.Run();
    node.Shutdown();
    
    return 0;
}
