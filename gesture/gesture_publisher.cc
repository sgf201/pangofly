#include <iostream>
#include <memory>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

#include "pangofly/pangofly.hpp"

extern "C" {
#include "media.h"
#include "k_vicap_comm.h"
}

struct ImageMessage {
    uint64_t timestamp;
    int width;
    int height;
    int format;  // 0: NV12, 1: RGB
    size_t data_size;
    char data[4096];
};

class GesturePublisher {
public:
    GesturePublisher() : running_(false), writer_(nullptr) {}
    
    ~GesturePublisher() {
        Stop();
    }
    
    int Init(const std::string& channel_name = "gesture_channel") {
        node_ = pangofly::CreateNode("gesture_publisher");
        if (!node_) {
            std::cerr << "Failed to create node" << std::endl;
            return -1;
        }
        
        writer_ = node_->CreateWriter<ImageMessage>(channel_name);
        if (!writer_) {
            std::cerr << "Failed to create writer" << std::endl;
            return -1;
        }
        
        std::cout << "Gesture Publisher initialized" << std::endl;
        return 0;
    }
    
    int Start() {
        if (running_) return 0;
        
        running_ = true;
        
        // Initialize media for camera capture
        KdMediaInputConfig config;
        config.video_valid = true;
        config.sensor_type = SENSOR_TYPE_MAX;
        config.sensor_num = 1;
        config.video_type = KdMediaVideoType::kVideoTypeMjpeg;
        config.venc_width = 640;
        config.venc_height = 480;
        config.bitrate_kbps = 2000;
        
        if (media_.Init(config) < 0) {
            std::cerr << "Failed to initialize media" << std::endl;
            return -1;
        }
        
        if (media_.CreateVcapVEnc(this) < 0) {
            std::cerr << "Failed to create VcapVEnc" << std::endl;
            return -1;
        }
        
        if (media_.StartVcapVEnc() < 0) {
            std::cerr << "Failed to start VcapVEnc" << std::endl;
            return -1;
        }
        
        std::cout << "Gesture Publisher started, capturing camera..." << std::endl;
        return 0;
    }
    
    int Stop() {
        if (!running_) return 0;
        
        running_ = false;
        
        media_.StopVcapVEnc();
        media_.DestroyVcapVEnc();
        media_.Deinit();
        
        writer_.reset();
        node_.reset();
        
        std::cout << "Gesture Publisher stopped" << std::endl;
        return 0;
    }
    
    void OnVEncData(k_u32 chn_id, void *data, size_t size, k_venc_pack_type type, uint64_t timestamp) override {
        if (!running_ || !writer_) return;
        
        ImageMessage msg;
        msg.timestamp = timestamp;
        msg.width = 640;
        msg.height = 480;
        msg.format = 1;  // MJPEG
        msg.data_size = std::min(size, sizeof(msg.data));
        memcpy(msg.data, data, msg.data_size);
        
        bool success = writer_->Write(msg);
        std::cout << "Published image: size=" << msg.data_size << ", success=" << success << std::endl;
    }
    
private:
    std::atomic<bool> running_;
    std::unique_ptr<pangofly::Node> node_;
    std::unique_ptr<pangofly::Writer<ImageMessage>> writer_;
    KdMedia media_;
};

int main() {
    GesturePublisher publisher;
    
    if (publisher.Init() != 0) {
        return -1;
    }
    
    if (publisher.Start() != 0) {
        return -1;
    }
    
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    
    publisher.Stop();
    return 0;
}
