#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>

#include "core/pangofly.h"
#include "idl/container/vector.h"
#include "idl/container/string.h"

struct Point3D {
    float x;
    float y;
    float z;
};

struct FaceDetectionResult {
    int32_t id;
    float score;
    Point3D bbox_min;
    Point3D bbox_max;
    pangofly::String name;
    pangofly::Vector<int32_t> landmarks;
};

struct DetectionResults {
    int64_t timestamp;
    pangofly::Vector<FaceDetectionResult> faces;
};

void WriterProcess() {
    std::cout << "Writer process started, PID=" << getpid() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto node = pangofly::CreateNode("nonpod_writer");
    auto writer = node->CreateWriter<DetectionResults>("nonpod_channel");
    
    DetectionResults results;
    results.timestamp = 1234567890LL;
    
    auto& face1 = results.faces.emplace_back();
    face1.id = 1;
    face1.score = 0.95f;
    face1.bbox_min = {100.0f, 100.0f, 0.0f};
    face1.bbox_max = {200.0f, 200.0f, 0.0f};
    face1.name = "Alice";
    face1.landmarks.push_back(10);
    face1.landmarks.push_back(20);
    face1.landmarks.push_back(30);
    face1.landmarks.push_back(40);
    
    auto& face2 = results.faces.emplace_back();
    face2.id = 2;
    face2.score = 0.87f;
    face2.bbox_min = {300.0f, 300.0f, 0.0f};
    face2.bbox_max = {400.0f, 400.0f, 0.0f};
    face2.name = "Bob";
    face2.landmarks.push_back(50);
    face2.landmarks.push_back(60);
    
    for (int i = 0; i < 5; ++i) {
        results.timestamp = 1234567890LL + i;
        bool success = writer->Write(results);
        std::cout << "Writer: Sent message id=" << i 
                  << ", faces=" << results.faces.size() 
                  << ", timestamp=" << results.timestamp << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "Writer done!" << std::endl;
}

void ReaderProcess() {
    std::cout << "Reader process started, PID=" << getpid() << std::endl;
    auto node = pangofly::CreateNode("nonpod_reader");
    auto reader = node->CreateReader<DetectionResults>("nonpod_channel");
    
    DetectionResults results;
    pangofly::MessageInfo info;
    
    for (int i = 0; i < 5; ++i) {
        bool success = reader->Read(&results, &info);
        if (success) {
            std::cout << "Reader: Received message seq=" << info.seq 
                      << ", timestamp=" << results.timestamp
                      << ", faces=" << results.faces.size() << std::endl;
            for (const auto& face : results.faces) {
                std::cout << "  Face: id=" << face.id 
                          << ", name=" << face.name.c_str()
                          << ", score=" << face.score << std::endl;
            }
        } else {
            std::cout << "Reader: No message available" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "Reader done!" << std::endl;
}

int main() {
    std::cout << "Starting Pangofly Non-POD SHM test (multi-process)..." << std::endl;
    
    pid_t writer_pid = fork();
    if (writer_pid == 0) {
        WriterProcess();
        return 0;
    } else if (writer_pid < 0) {
        std::cerr << "Failed to fork writer process" << std::endl;
        return 1;
    }
    
    pid_t reader_pid = fork();
    if (reader_pid == 0) {
        ReaderProcess();
        return 0;
    } else if (reader_pid < 0) {
        std::cerr << "Failed to fork reader process" << std::endl;
        kill(writer_pid, SIGKILL);
        return 1;
    }
    
    int status;
    waitpid(writer_pid, &status, 0);
    waitpid(reader_pid, &status, 0);
    
    std::cout << "Non-POD Test completed!" << std::endl;
    return 0;
}
