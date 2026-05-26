// Pangofly Gesture Publisher
// Reads camera images and publishes via Pangofly shared memory
// This version uses simple POD types for compatibility

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>

#include "pangofly/pangofly.h"

// Simple POD message type for testing
struct TestMessage {
  uint64_t timestamp;
  uint32_t value;
  char name[64];
};

void PublisherProcess() {
  std::cout << "Gesture Publisher starting, PID: " << getpid() << std::endl;
  
  // Initialize pangofly
  auto node = pangofly::CreateNode("gesture_publisher");
  auto writer = node->CreateWriter<TestMessage>("gesture_channel");
  
  for (int i = 0; i < 100; i++) {
    TestMessage msg;
    msg.timestamp = i;
    msg.value = 314 + i;
    strcpy(msg.name, "Gesture Test");
    
    bool success = writer->Write(msg);
    if (success && i % 10 == 0) {
      std::cout << "Published message " << i << ", success: " << success << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  
  std::cout << "Publisher done!" << std::endl;
}

int main(int argc, char** argv) {
  PublisherProcess();
  return 0;
}
