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
  std::cout << "Starting Pangofly SHM Writer..." << std::endl;
  
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  auto node = pangofly::CreateNode("writer_node");
  auto writer = node->CreateWriter<TestMessage>("test_channel");
  
  TestMessage msg;
  msg.value = 3.14;
  strcpy(msg.name, "Hello Pangofly");
  
  std::cout << "Writer: Creating shared memory channel..." << std::endl;
  
  for (int i = 0; i < 10; ++i) {
    msg.id = i;
    bool success = writer->Write(msg);
    std::cout << "Writer: Sent message id=" << msg.id << ", success=" << success << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  
  std::cout << "Writer: All 10 messages sent!" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::cout << "Writer done!" << std::endl;
  return 0;
}
