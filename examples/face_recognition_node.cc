#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <random>

#include "pangofly/pangofly.h"
#include "pangofly/node/node.h"
#include "idl/generated/face_detection.h"

using namespace pangofly;
using namespace FaceDetection;

class FaceRecognitionNode {
public:
    FaceRecognitionNode() : node_(nullptr), reader_(), writer_() {}
    
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

        std::cout << "\n[FaceRecognition] Received frame: "
                  << frame.width << "x" << frame.height
                  << ", data_size: " << frame.data.size()
                  << ", timestamp: " << frame.timestamp << std::endl;

        int32_t face_count = simulate_random_faces();

        Sample<FaceResult> sample = writer_->LoanSample(sizeof(FaceResult) + face_count * sizeof(FaceBox) + face_count * 5 * sizeof(FaceLandmark));
        if (!sample.IsValid()) {
            std::cerr << "[FaceRecognition] Failed to loan sample" << std::endl;
            return;
        }

        FaceResult* result = sample.message;
        result->frame_id = frame_id_++;
        result->face_count = face_count;

        result->faces.resize(face_count);
        result->landmarks.resize(face_count * 5);

        for (int i = 0; i < face_count; ++i) {
            FaceBox face;
            face.id = i + 1;
            face.score = static_cast<float>(0.8 + (std::rand() % 20) / 100.0);

            int margin = 50;
            face.x = margin + std::rand() % (frame.width - margin * 2 - 100);
            face.y = margin + std::rand() % (frame.height - margin * 2 - 100);
            face.width = 60 + std::rand() % 80;
            face.height = 60 + std::rand() % 80;

            result->faces[i] = face;

            for (int j = 0; j < 5; ++j) {
                FaceLandmark landmark;
                landmark.x = face.x + std::rand() % face.width;
                landmark.y = face.y + std::rand() % face.height;
                result->landmarks[i * 5 + j] = landmark;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result->processing_time_ms = static_cast<float>(duration.count());
        result->timestamp = frame.timestamp;

        if (writer_->Write(sample)) {
            std::cout << "[FaceRecognition] Detection completed in " << duration.count() << "ms" << std::endl;
            std::cout << "[FaceRecognition] Detected " << result->face_count << " faces" << std::endl;

            for (int i = 0; i < result->face_count; ++i) {
                const FaceBox& face = result->faces[i];
                std::cout << "[FaceRecognition]   Face ID: " << face.id
                          << ", Score: " << face.score
                          << ", Box: (" << face.x << "," << face.y << ")-("
                          << face.x + face.width << "," << face.y + face.height << ")" << std::endl;
            }
        } else {
            std::cerr << "[FaceRecognition] Failed to send face result" << std::endl;
            writer_->Release(sample);
        }
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