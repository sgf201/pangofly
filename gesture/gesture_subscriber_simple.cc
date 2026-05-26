#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "../pangofly/pangofly.h"

using namespace std;

// 简单的图像消息结构（POD类型）- 必须与publisher保持一致
const int MAX_IMAGE_SIZE = 256 * 256 * 3;  // 256x256 RGB图像

struct ImageMessage {
    int64_t timestamp;
    int32_t width;
    int32_t height;
    int32_t format;
    uint8_t data[MAX_IMAGE_SIZE];
};

int main() {
    cout << "Starting gesture subscriber (simple POD)..." << endl;
    pangofly::Init(nullptr);
    
    auto node = pangofly::CreateNode("gesture_subscriber");
    auto reader = node->CreateReader<ImageMessage>("gesture_channel");
    
    cout << "Subscriber ready, waiting for images..." << endl;
    
    int frame_count = 0;
    while (true) {
        ImageMessage msg;
        pangofly::MessageInfo info;
        bool success = reader->Read(&msg, &info);
        
        if (success) {
            int data_size = msg.width * msg.height * 3;
            cout << "Received frame " << frame_count 
                 << " timestamp=" << msg.timestamp 
                 << " size=" << data_size << " bytes" 
                 << " seq=" << info.seq << endl;
            
            // 简单验证接收到的数据
            if (data_size > 0 && data_size <= MAX_IMAGE_SIZE) {
                cout << "  First byte: " << (int)msg.data[0] 
                     << " Last byte: " << (int)msg.data[data_size-1] << endl;
            }
            frame_count++;
        } else {
            cout << "No message available" << endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (frame_count > 10) break;
    }
    
    cout << "Subscriber exiting." << endl;
    return 0;
}
