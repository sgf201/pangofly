// v2 跨线程示例：Loan 在生产者线程，填充 + Publish 在消费者线程
//
// 这个例子演示 Sample<T> 的所有权可以安全地跨线程传递：
//   * Sample<T> 内部持 shared_ptr<Segment>，Segment 生命周期不依赖 Writer 句柄
//   * Sample 是 move-only，转移所有权不会复制任何数据
//   * 消息对象本身始终在共享内存里，跨线程传递的只是一个指针 + 块句柄
//
// 用法：./shm_thread_pipeline [帧数]
//
// 流程：
//   [acq 线程]  Loan() 抢块 → 塞进队列
//   [fill 线程] 从队列取出 → 在共享内存中填像素/检测框 → Publish()
//   [main]     等两个线程结束，打印统计
//
// 与单线程 writer.cc 的对比：这里 Loan 与 Publish 不在同一线程，
// 因此不能使用 Writer::Write(fn) 这种「借-填-发」一体化接口，
// 必须显式持有 Sample<T> 并在另一个线程完成。

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "core/pangofly.h"
#include "examples/common/shm_example/messages.h"

namespace {

// Loan 线程与 Fill 线程之间的队列。只传递 Sample 的所有权，不传数据。
using SampleQueue = std::deque<pangofly::shm::Sample<example::ImageFrame>>;

struct Channel {
  std::mutex mu;
  std::condition_variable cv;
  SampleQueue queue;
  bool closed = false;

  void Push(pangofly::shm::Sample<example::ImageFrame> s) {
    {
      std::lock_guard<std::mutex> lk(mu);
      queue.push_back(std::move(s));   // 仅转移指针，无拷贝
    }
    cv.notify_one();
  }

  // 阻塞取一个；队列关闭且已空时返回无效 Sample
  pangofly::shm::Sample<example::ImageFrame> Pop() {
    std::unique_lock<std::mutex> lk(mu);
    cv.wait(lk, [this] { return !queue.empty() || closed; });
    if (queue.empty()) return {};
    auto s = std::move(queue.front());
    queue.pop_front();
    return s;
  }

  void Close() {
    {
      std::lock_guard<std::mutex> lk(mu);
      closed = true;
    }
    cv.notify_all();
  }
};

}  // namespace

int main(int argc, char** argv) {
  const int frames = argc > 1 ? std::atoi(argv[1]) : 50;

  if (!pangofly::Init()) {
    std::fprintf(stderr, "[pipeline] Init failed\n");
    return 1;
  }

  auto node = pangofly::CreateNode("thread_pipeline");
  auto writer = node->CreateWriter<example::ImageFrame>("thread_channel");
  if (!writer) {
    std::fprintf(stderr, "[pipeline] CreateWriter failed\n");
    return 1;
  }

  const int w = 640, h = 480;
  const size_t bytes = static_cast<size_t>(w) * h * 3;
  std::printf("[pipeline] cap=%u block_num=%u frames=%d\n", writer->Capacity(),
              writer->BlockNum(), frames);

  Channel ch;
  std::atomic<int> loaned{0};
  std::atomic<int> published{0};
  std::atomic<int> loan_failed{0};
  std::atomic<bool> acq_done{false};

  // ---- 线程 A：只负责 Loan ----------------------------------------------
  // 注意：Loan 与 Publish 分离时，块会一直处于 WRITING 状态直到有人发布。
  // 若生产者比消费者快，块会被快速耗尽，Loan 返回无效 Sample（背压），
  // 这里选择稍等重试，模拟真实采集线程的节流行为。
  std::thread producer([&] {
    for (int i = 0; i < frames; ++i) {
      // 有界队列：最多领先消费者 block_num 帧，避免无限积压
      while (true) {
        {
          std::lock_guard<std::mutex> lk(ch.mu);
          if (ch.queue.size() < writer->BlockNum()) break;
        }
        usleep(1000);
      }

      auto s = writer->Loan();
      if (!s.valid()) {
        ++loan_failed;
        usleep(1000);
        --i;   // 本帧没借到，重试
        continue;
      }
      // Sample 在这里被 move 进队列：所有权（块句柄 + shared_ptr<Segment>）
      // 转移到 fill 线程，消息对象仍在共享内存中原地不动。
      ch.Push(std::move(s));
      ++loaned;
    }
    acq_done = true;
  });

  // ---- 线程 B：只负责填充并发布 -----------------------------------------
  std::thread consumer([&] {
    while (true) {
      auto s = ch.Pop();
      if (!s.valid()) break;   // 队列关闭且为空

      // 此刻 s 指向共享内存中的 ImageFrame；下面每次赋值都直接落到 shm
      s->timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
      s->width = w;
      s->height = h;
      s->stride = w * 3;
      s->label.assign("pipeline");

      // 大数据填充：直接写 shm 的 arena，中间没有任何临时缓冲
      s->pixels.resize(bytes);
      std::memset(s->pixels.data(), static_cast<uint8_t>(published.load() & 0xFF), bytes);

      s->detections.resize(1);
      s->detections[0].x = published.load();
      s->detections[0].y = 0;
      s->detections[0].w = 10;
      s->detections[0].h = 10;
      s->detections[0].score = 0.88f;

      // Publish 只翻块状态位（WRITING -> PUBLISHED），不搬数据。
      // 这里用的 writer 句柄与 Loan 时的线程不同，但句柄本身是线程安全的
      // 轻量对象（只读 shared_ptr + 原子状态）。
      if (!writer->Publish(std::move(s))) {
        std::fprintf(stderr, "[pipeline] Publish failed\n");
        break;
      }
      ++published;
    }
  });

  producer.join();
  ch.Close();   // 让 consumer 在队列排空后退出
  consumer.join();

  // 等读端把最后几帧取走，避免进程退出时块仍被标记为已发布
  usleep(200000);

  std::printf("[pipeline] loaned=%d published=%d loan_failed=%d\n", loaned.load(),
              published.load(), loan_failed.load());
  return published.load() == frames ? 0 : 1;
}
