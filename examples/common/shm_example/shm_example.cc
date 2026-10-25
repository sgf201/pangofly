// v2 单进程自测：
//   1) 写满所有块再逐条读，校验内容与容器语义
//   2) 读完后块被归还，继续写触发「回收最旧块」路径

#include <cstdio>
#include <cstring>
#include <utility>

#include "core/pangofly.h"
#include "examples/common/shm_example/messages.h"

static void Fill(example::ImageFrame* m, int i) {
  m->timestamp_ns = static_cast<int64_t>(i);
  m->width = 4;
  m->height = 2;
  m->stride = 12;
  m->pixels.resize(24);
  for (int k = 0; k < 24; ++k) m->pixels[k] = static_cast<uint8_t>(k + i);
  m->detections.resize(1);
  m->detections[0].x = i;
  m->detections[0].score = 0.5f;
  m->label.assign("t");
}

static bool Check(const example::ImageFrame& m) {
  if (m.pixels.size() != 24 || m.detections.size() != 1) return false;
  if (m.detections[0].x != static_cast<int32_t>(m.timestamp_ns)) return false;
  if (std::strcmp(m.label.c_str(), "t") != 0) return false;
  for (int k = 0; k < 24; ++k) {
    if (m.pixels[k] != static_cast<uint8_t>(k + m.timestamp_ns)) return false;
  }
  return true;
}

int main() {
  if (!pangofly::Init()) {
    std::fprintf(stderr, "Init failed\n");
    return 1;
  }

  auto node = pangofly::CreateNode("self_test");
  auto writer = node->CreateWriter<example::ImageFrame>("self_channel");
  auto reader = node->CreateReader<example::ImageFrame>("self_channel");
  if (!writer || !reader) {
    std::fprintf(stderr, "create failed\n");
    return 1;
  }

  const uint32_t n = writer->BlockNum();
  std::printf("block_num=%u cap=%u\n", n, writer->Capacity());

  // 阶段 1：写满所有块
  for (uint32_t i = 1; i <= n; ++i) {
    auto s = writer->Loan();
    if (!s.valid()) {
      std::fprintf(stderr, "stage1 Loan failed at %u\n", i);
      return 1;
    }
    Fill(s.get(), static_cast<int>(i));
    if (!writer->Publish(std::move(s))) return 1;
  }

  // 块已用尽：此时再借应当走「回收无读者旧块」或返回背压
  {
    auto s = writer->Loan();
    std::printf("stage1 extra Loan valid=%d (expect 1: recycle oldest)\n", s.valid() ? 1 : 0);
    if (!s.valid()) {
      std::fprintf(stderr, "expected recycle path\n");
      return 1;
    }
    Fill(s.get(), 999);
    if (!writer->Publish(std::move(s))) return 1;
  }

  // 阶段 2：把剩下的都读出来
  int got = 0;
  bool all_ok = true;
  while (true) {
    auto v = reader->Acquire();
    if (!v.valid()) break;
    ++got;
    if (!Check(*v)) {
      std::printf("  MISMATCH ts=%lld\n", static_cast<long long>(v->timestamp_ns));
      all_ok = false;
    }
  }
  std::printf("stage2 read %d messages, content_ok=%d\n", got, all_ok ? 1 : 0);
  if (!all_ok || got == 0) return 1;

  // 阶段 3：读完后块已归还，应能再次借满
  int loaned = 0;
  for (uint32_t i = 0; i < n; ++i) {
    auto s = writer->Loan();
    if (!s.valid()) break;
    Fill(s.get(), static_cast<int>(i));
    if (!writer->Publish(std::move(s))) return 1;
    ++loaned;
  }
  std::printf("stage3 loaned=%d (expect %u)\n", loaned, n);

  // 阶段 4：keep_last——收端最多保留 history 条
  // 新建一个 history=2 的通道，连发 5 条后只应能读到最新的 2 条。
  {
    pangofly::shm::SegmentConfig cfg;
    cfg.block_num = 6;
    cfg.block_capacity = 1u << 20;
    cfg.history = 2;
    auto hw = node->CreateWriter<example::ImageFrame>("history_channel", cfg);
    auto hr = node->CreateReader<example::ImageFrame>("history_channel", cfg);
    if (!hw || !hr) {
      std::fprintf(stderr, "history channel create failed\n");
      return 1;
    }
    std::printf("stage4 history=%u block_num=%u\n", hw->History(), hw->BlockNum());

    for (int i = 1; i <= 5; ++i) {
      auto s = hw->Loan();
      if (!s.valid()) {
        std::fprintf(stderr, "stage4 Loan failed at %d\n", i);
        return 1;
      }
      Fill(s.get(), i);
      if (!hw->Publish(std::move(s))) return 1;
    }

    int kept = 0;
    int64_t first_ts = -1;
    while (true) {
      auto v = hr->Acquire();
      if (!v.valid()) break;
      if (first_ts < 0) first_ts = v->timestamp_ns;
      ++kept;
    }
    // 期望：只读到 2 条，且是最新的 ts=4、5
    const bool ok = (kept == 2 && first_ts == 4);
    std::printf("stage4 kept=%d first_ts=%lld (expect 2 / 4) -> %s\n", kept,
                static_cast<long long>(first_ts), ok ? "PASS" : "FAIL");
    if (!ok) return 1;
  }

  return loaned == static_cast<int>(n) ? 0 : 1;
}
