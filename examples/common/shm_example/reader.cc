#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>

#include "pangofly/pangofly.h"

struct TestMessage {
  int id;
  double value;
  char name[64];
  
  TestMessage() : id(0), value(0.0) {
    memset(name, 0, sizeof(name));
  }
  
  explicit TestMessage(pangofly::BlockAllocator* /*allocator*/) : id(0), value(0.0) {
    memset(name, 0, sizeof(name));
  }
  
  size_t CalculateSize() const {
    return sizeof(TestMessage);
  }
};

int main() {
  std::cout << "Starting Pangofly SHM Reader..." << std::endl;
  
  auto node = pangofly::CreateNode("reader_node");
  auto reader = node->CreateReader<TestMessage>("test_channel");
  
  TestMessage msg;
  pangofly::MessageInfo info;
  
  int count = 0;
  while (true) {
    bool success = reader->Read(&msg, &info);
    if (success) {
      std::cout << "Reader: Received message id=" << msg.id << ", value=" << msg.value 
                << ", name=" << msg.name << ", seq=" << info.seq << std::endl;
      count++;
      if (msg.id == 9 && count >= 9) {
        std::cout << "Reader: All 10 messages received!" << std::endl;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  std::cout << "Reader done!" << std::endl;
  return 0;
}
