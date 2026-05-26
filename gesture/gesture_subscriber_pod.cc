// Pangofly Gesture Subscriber
// Receives images and runs gesture recognition
// This version uses simple POD types for compatibility

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>

#include "pangofly/pangofly.h"

struct TestMessage {
  uint64_t timestamp;
  uint32_t value;
  char name[64];
};

void SubscriberProcess() {
  std::cout << "Gesture Subscriber starting, PID: " << getpid() << std::endl;
  
  auto node = pangofly::CreateNode("gesture_subscriber");
  auto reader = node->CreateReader<TestMessage>("gesture_channel");
  
  TestMessage msg;
  pangofly::MessageInfo info;
  
  for (int i = 0; i < 100; i++) {
    bool success = reader->Read(&msg, &info);
    if (success) {
      std::cout << "Received message: timestamp=" << msg.timestamp 
                << ", value=" << msg.value 
                << ", name=" << msg.name 
                << ", seq=" << info.seq << std::endl;
    } else {
      std::cout << "No message available" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  
  std::cout << "Subscriber done!" << std::endl;
}

int main(int argc, char** argv) {
  SubscriberProcess();
  return 0;
}
