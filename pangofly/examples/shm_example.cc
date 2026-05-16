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
};

void WriterThread() {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  auto node = pangofly::CreateNode("writer_node");
  auto writer = node->CreateWriter<TestMessage>("test_channel");
  
  TestMessage msg;
  msg.id = 1;
  msg.value = 3.14;
  strcpy(msg.name, "Hello Pangofly");
  
  for (int i = 0; i < 10; ++i) {
    msg.id = i;
    bool success = writer->Write(msg);
    std::cout << "Writer: Sent message id=" << msg.id << ", value=" << msg.value << ", name=" << msg.name << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  
  std::cout << "Writer done!" << std::endl;
}

void ReaderThread() {
  auto node = pangofly::CreateNode("reader_node");
  auto reader = node->CreateReader<TestMessage>("test_channel");
  
  TestMessage msg;
  pangofly::MessageInfo info;
  
  for (int i = 0; i < 10; ++i) {
    bool success = reader->Read(&msg, &info);
    if (success) {
      std::cout << "Reader: Received message id=" << msg.id << ", value=" << msg.value 
                << ", name=" << msg.name << ", seq=" << info.seq << std::endl;
    } else {
      std::cout << "Reader: No message available" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  
  std::cout << "Reader done!" << std::endl;
}

int main() {
  std::cout << "Starting Pangofly SHM test..." << std::endl;
  
  std::thread writer(WriterThread);
  std::thread reader(ReaderThread);
  
  writer.join();
  reader.join();
  
  std::cout << "Test completed!" << std::endl;
  return 0;
}