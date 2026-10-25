// v2 读端示例：直接读共享内存中的对象，无拷贝
//
// 用法：./shm_reader [期望帧数] [超时ms]

#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "core/pangofly.h"
#include "examples/common/shm_example/messages.h"

static volatile std::sig_atomic_t g_running = 1;
static void OnSignal(int) { g_running = 0; }

int main(int argc, char** argv) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  int expect = argc > 1 ? std::atoi(argv[1]) : 100;
  uint64_t timeout_ns = (argc > 2 ? std::atoi(argv[2]) : 5000ull) * 1000000ull;

  if (!pangofly::Init()) {
    std::fprintf(stderr, "[reader] pangofly::Init failed\n");
    return 1;
  }

  auto node = pangofly::CreateNode("shm_reader");
  auto reader = node->CreateReader<example::ImageFrame>("image_channel");
  if (!reader) {
    std::fprintf(stderr, "[reader] CreateReader failed\n");
    return 1;
  }

  int got = 0;
  while (got < expect && g_running) {
    auto v = reader->Wait(timeout_ns);
    if (!v.valid()) {
      std::fprintf(stderr, "[reader] timeout after %d\n", got);
      break;
    }
    ++got;
    // v 指向共享内存，直接读，没有任何拷贝
    std::printf("[reader] seq=%llu %dx%d stride=%d %zu bytes tag=%s dets=%zu"
                " ptr=%p\n",
                static_cast<unsigned long long>(v.seq()), v->width, v->height,
                v->stride, v->pixels.size(), v->label.c_str(),
                v->detections.size(), static_cast<const void*>(v.get()));
    if (!v->detections.empty()) {
      std::printf("         det0=(%d,%d,%d,%d) score=%.2f\n", v->detections[0].x,
                  v->detections[0].y, v->detections[0].w, v->detections[0].h,
                  v->detections[0].score);
    }
  }

  std::printf("[reader] read %d frames\n", got);
  return got > 0 ? 0 : 1;
}
