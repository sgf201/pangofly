// v2 写端示例：直接在共享内存中构造消息，无拷贝
//
// 用法：./shm_writer [帧数] [间隔ms]

#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/pangofly.h"
#include "examples/common/shm_example/messages.h"

static volatile std::sig_atomic_t g_running = 1;
static void OnSignal(int) { g_running = 0; }

int main(int argc, char** argv) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  int frames = argc > 1 ? std::atoi(argv[1]) : 100;
  int interval_ms = argc > 2 ? std::atoi(argv[2]) : 33;

  if (!pangofly::Init()) {
    std::fprintf(stderr, "[writer] pangofly::Init failed\n");
    return 1;
  }

  auto node = pangofly::CreateNode("shm_writer");
  auto writer = node->CreateWriter<example::ImageFrame>("image_channel");
  if (!writer) {
    std::fprintf(stderr, "[writer] CreateWriter failed\n");
    return 1;
  }

  const int w = 640, h = 480;
  const size_t bytes = static_cast<size_t>(w) * h * 3;

  std::printf("[writer] cap=%u block_num=%u\n", writer->Capacity(), writer->BlockNum());

  for (int i = 0; i < frames && g_running; ++i) {
    auto s = writer->Loan();
    if (!s.valid()) {
      std::fprintf(stderr, "[writer] Loan failed at %d (back-pressure)\n", i);
      usleep(interval_ms * 1000);
      continue;
    }

    s->timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    s->width = w;
    s->height = h;
    s->stride = w * 3;
    s->label.assign("frame");

    // 直接写共享内存
    s->pixels.resize(bytes);
    std::memset(s->pixels.data(), static_cast<uint8_t>(i & 0xFF), bytes);

    s->detections.resize(2);
    s->detections[0].x = 10 + i;
    s->detections[0].y = 20;
    s->detections[0].w = 100;
    s->detections[0].h = 120;
    s->detections[0].score = 0.9f;
    s->detections[1].x = 300;
    s->detections[1].y = 200;
    s->detections[1].w = 80;
    s->detections[1].h = 60;
    s->detections[1].score = 0.75f;

    if (!writer->Publish(std::move(s))) {
      std::fprintf(stderr, "[writer] Publish failed at %d\n", i);
    } else if (i < 3 || (i + 1) % 20 == 0) {
      std::printf("[writer] sent frame %d (%dx%d, %zu bytes, 2 dets)\n", i, w, h, bytes);
    }

    usleep(interval_ms * 1000);
  }

  std::printf("[writer] done\n");
  return 0;
}
