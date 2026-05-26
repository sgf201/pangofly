#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "../pangofly/pangofly.h"

using namespace std;

// 简单的图像消息结构（POD类型）
const int MAX_IMAGE_SIZE = 256 * 256 * 3;  // 256x256 RGB图像

struct ImageMessage {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    uint8_t data[MAX_IMAGE_SIZE];
};

int main() {
    cout << "Starting gesture publisher (simple POD)..." << endl;
    pangofly::Init(nullptr);
    
    auto node = pangofly::CreateNode("gesture_publisher");
    auto writer = node->CreateWriter<ImageMessage>("gesture_channel");
    
    cout << "Publisher ready, sending test images..." << endl;
    
    int frame_count = 0;
    while (true) {
        ImageMessage msg;
        msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.width = 256;
        msg.height = 256;
        msg.format = 0;
        
        // 创建简单的测试图像数据
        int data_size = msg.width * msg.height * 3;
        for (int i = 0; i < data_size; i++) {
            msg.data[i] = (uint8_t)((frame_count * 17 + i) % 256);
        }
        
        bool success = writer->Write(msg);
        if (success) {
            cout << "Sent frame " << frame_count 
                 << " timestamp=" << msg.timestamp 
                 << " size=" << data_size << " bytes" << endl;
        } else {
            cout << "Failed to send frame " << frame_count << endl;
        }
        
        frame_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (frame_count > 100) break;
    }
    
    cout << "Publisher exiting." << endl;
    return 0;
}
