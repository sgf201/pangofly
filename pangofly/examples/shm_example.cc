#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>

#include "pangofly/pangofly.h"

struct TestMessage {
  int id;
  double value;
  char name[64];
};

void WriterProcess() {
  std::cout << "Writer process started, PID=" << getpid() << std::endl;
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

void ReaderProcess() {
  std::cout << "Reader process started, PID=" << getpid() << std::endl;
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
  std::cout << "Starting Pangofly SHM test (multi-process)..." << std::endl;
  
  pid_t writer_pid = fork();
  if (writer_pid == 0) {
    // Writer process
    WriterProcess();
    return 0;
  } else if (writer_pid < 0) {
    std::cerr << "Failed to fork writer process" << std::endl;
    return 1;
  }
  
  pid_t reader_pid = fork();
  if (reader_pid == 0) {
    // Reader process
    ReaderProcess();
    return 0;
  } else if (reader_pid < 0) {
    std::cerr << "Failed to fork reader process" << std::endl;
    kill(writer_pid, SIGKILL);
    return 1;
  }
  
  // Parent process waits
  int status;
  waitpid(writer_pid, &status, 0);
  waitpid(reader_pid, &status, 0);
  
  std::cout << "Test completed!" << std::endl;
  return 0;
}