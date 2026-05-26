#include "gesture_app.h"
#include "../pangofly/pangofly.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

const int MAX_IMAGE_SIZE = 640 * 480 * 3;

struct ImageFrame {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    uint8_t data[MAX_IMAGE_SIZE];
};

GesturePublisher::GesturePublisher(const std::string& channel_name) 
    : channel_name_(channel_name), video_device_(0) {
}

GesturePublisher::~GesturePublisher() {
    Stop();
}

bool GesturePublisher::Init(int video_device) {
    video_device_ = video_device;
    pangofly::Init(nullptr);
    return true;
}

bool GesturePublisher::Start() {
    if (running_.load()) return false;
    
    running_.store(true);
    capture_thread_ = std::make_unique<std::thread>(&GesturePublisher::CaptureLoop, this);
    return true;
}

void GesturePublisher::Stop() {
    running_.store(false);
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
}

void GesturePublisher::CaptureLoop() {
    auto node = pangofly::CreateNode("gesture_publisher");
    auto writer = node->CreateWriter<ImageFrame>(channel_name_);
    
    std::cout << "Gesture publisher started on channel: " << channel_name_ << std::endl;
    
    int frame_count = 0;
    while (running_.load()) {
        ImageFrame frame;
        frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        frame.width = 256;
        frame.height = 256;
        frame.format = 0;
        
        int data_size = frame.width * frame.height * 3;
        for (int i = 0; i < data_size; i++) {
            frame.data[i] = (uint8_t)((frame_count * 17 + i) % 256);
        }
        
        bool success = writer->Write(frame);
        if (success) {
            std::cout << "Published frame " << frame_count << " timestamp=" << frame.timestamp << std::endl;
        }
        
        frame_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Gesture publisher stopped" << std::endl;
}

GestureSubscriber::GestureSubscriber(const std::string& channel_name) 
    : channel_name_(channel_name) {
}

GestureSubscriber::~GestureSubscriber() {
    Stop();
}

bool GestureSubscriber::Init(const std::string& model_path) {
    model_path_ = model_path;
    pangofly::Init(nullptr);
    return true;
}

bool GestureSubscriber::Start() {
    if (running_.load()) return false;
    
    running_.store(true);
    process_thread_ = std::make_unique<std::thread>(&GestureSubscriber::ProcessLoop, this);
    return true;
}

void GestureSubscriber::Stop() {
    running_.store(false);
    if (process_thread_ && process_thread_->joinable()) {
        process_thread_->join();
    }
}

std::vector<GestureResult> GestureSubscriber::GetResults() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
}

void GestureSubscriber::ProcessLoop() {
    auto node = pangofly::CreateNode("gesture_subscriber");
    auto reader = node->CreateReader<ImageFrame>(channel_name_);
    
    std::cout << "Gesture subscriber started on channel: " << channel_name_ << std::endl;
    
    while (running_.load()) {
        ImageFrame frame;
        pangofly::MessageInfo info;
        bool success = reader->Read(&frame, &info);
        
        if (success) {
            std::cout << "Received frame timestamp=" << frame.timestamp 
                      << " size=" << frame.width << "x" << frame.height << std::endl;
            
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.clear();
            
            GestureResult result;
            result.timestamp = frame.timestamp;
            result.gesture = "unknown";
            result.confidence = 0.0f;
            result.x = 0;
            result.y = 0;
            result.width = frame.width;
            result.height = frame.height;
            results_.push_back(result);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    std::cout << "Gesture subscriber stopped" << std::endl;
}