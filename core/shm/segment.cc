#include "core/shm/segment.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>

#include "core/common/log.h"
#include "core/shm/arena.h"

namespace pangofly {
namespace shm {
namespace {

std::string ShmName(const std::string& channel) {
  // 用完整 channel 名做 shm 名，避免哈希碰撞
  std::string safe = channel;
  for (char& c : safe) {
    if (c == '/') c = '_';
  }
  return "/pangofly_v2_" + safe;
}

uint64_t NowNs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// 固定地址映射：先探测再落盘，失败即返回 nullptr，绝不回退到任意地址。
void* MapFixed(int fd, size_t size, uintptr_t addr) {
#if defined(MAP_FIXED_NOREPLACE)
  void* probe = mmap(reinterpret_cast<void*>(addr), size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (probe == MAP_FAILED) {
    PANGOFLY_ERROR << "[segment] VA occupied: 0x" << std::hex << addr << std::dec
                   << std::endl;
    return nullptr;
  }
  munmap(probe, size);
#endif
  void* p = mmap(reinterpret_cast<void*>(addr), size, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fd, 0);
  if (p == MAP_FAILED) {
    PANGOFLY_ERROR << "[segment] MAP_FIXED failed at 0x" << std::hex << addr << std::dec
                   << std::endl;
    return nullptr;
  }
  if (reinterpret_cast<uintptr_t>(p) != addr) {
    PANGOFLY_ERROR << "[segment] unexpected mapping address 0x" << std::hex
                   << reinterpret_cast<uintptr_t>(p) << " expect 0x" << addr << std::dec
                   << std::endl;
    munmap(p, size);
    return nullptr;
  }
  return p;
}

}  // namespace

Segment::~Segment() {
  if (base_ != nullptr) {
    munmap(base_, total_size_);
    base_ = nullptr;
    header_ = nullptr;
  }
  if (window_.valid()) {
    Region::Release(window_);
    window_ = Window{};
  }
  // 不在这里 shm_unlink：生命周期由外部显式管理，
  // 避免写端退出后读端无法 attach。
}

size_t Segment::CalcArenaBytes(const SegmentConfig& cfg) {
  return AlignUp(Arena::RequiredBytes(cfg.block_capacity), 64);
}

size_t Segment::CalcTotalSize(const SegmentConfig& cfg) {
  const size_t header_bytes = sizeof(SegmentHeader) + sizeof(BlockHeader) * cfg.block_num;
  return AlignUp(header_bytes, 64) + CalcArenaBytes(cfg) * cfg.block_num;
}

BlockHeader* Segment::HeaderAt(uint32_t i) {
  auto* p = reinterpret_cast<uint8_t*>(base_) + sizeof(SegmentHeader);
  return reinterpret_cast<BlockHeader*>(p) + i;
}

const BlockHeader* Segment::HeaderAt(uint32_t i) const {
  auto* p = reinterpret_cast<const uint8_t*>(base_) + sizeof(SegmentHeader);
  return reinterpret_cast<const BlockHeader*>(p) + i;
}

bool Segment::OpenOrCreate(const std::string& channel, const SegmentConfig& cfg) {
  if (header_ != nullptr) return true;
  if (cfg.block_num == 0 || cfg.block_capacity < 4096) {
    PANGOFLY_ERROR << "[segment] invalid config" << std::endl;
    return false;
  }
  channel_ = channel;

  const std::string name = ShmName(channel);

  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  bool creator = (fd >= 0);

  if (!creator) {
    if (errno != EEXIST) {
      PANGOFLY_ERROR << "[segment] shm_open failed for " << name << std::endl;
      return false;
    }
    fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd < 0) {
      PANGOFLY_ERROR << "[segment] shm_open(existing) failed for " << name << std::endl;
      return false;
    }
  }

  // 打开者以共享内存对象的实际大小为准，而不是调用方传入的 cfg。
  // 否则「读端不知道写端的 block_num / block_capacity」就永远无法 attach，
  // 这是比配置校验更重要的可用性要求。
  size_t total = 0;
  if (creator) {
    total = CalcTotalSize(cfg);
    if (ftruncate(fd, static_cast<off_t>(total)) < 0) {
      PANGOFLY_ERROR << "[segment] ftruncate failed" << std::endl;
      close(fd);
      shm_unlink(name.c_str());
      return false;
    }
  } else {
    // 创建者可能刚 shm_open 完还没 ftruncate，此时 size 为 0，稍等重试
    struct stat st{};
    for (int spin = 0; spin < 2000; ++spin) {
      if (fstat(fd, &st) == 0 && st.st_size > 0) break;
      struct timespec req{0, 1000000};   // 1ms
      nanosleep(&req, nullptr);
    }
    if (st.st_size <= 0) {
      PANGOFLY_ERROR << "[segment] fstat failed for " << name << std::endl;
      close(fd);
      return false;
    }
    total = static_cast<size_t>(st.st_size);
  }

  Window w = Region::Acquire(channel, total);
  if (!w.valid()) {
    PANGOFLY_ERROR << "[segment] no window for " << channel << " need=" << total
                   << std::endl;
    close(fd);
    if (creator) shm_unlink(name.c_str());
    return false;
  }
  if (w.size < total) {
    PANGOFLY_ERROR << "[segment] window too small: size=" << w.size
                   << " need=" << total << std::endl;
    close(fd);
    Region::Release(w);
    if (creator) shm_unlink(name.c_str());
    return false;
  }

  void* mem = MapFixed(fd, total, w.addr);
  if (mem == nullptr) {
    close(fd);
    Region::Release(w);
    if (creator) shm_unlink(name.c_str());
    return false;
  }
  close(fd);

  base_ = mem;
  total_size_ = total;
  window_ = w;
  is_creator_ = creator;

  auto* hdr = static_cast<SegmentHeader*>(mem);

  if (creator) {
    std::memset(mem, 0, total);
    hdr->version = kSegmentVersion;
    hdr->block_num = cfg.block_num;
    hdr->block_capacity = cfg.block_capacity;
    hdr->chunk_size = static_cast<uint32_t>(CalcArenaBytes(cfg));
    hdr->total_size = total;
    hdr->self_base = reinterpret_cast<uint64_t>(mem);
    hdr->next_seq.store(1, std::memory_order_relaxed);
    hdr->payload_base = static_cast<uint32_t>(
        AlignUp(sizeof(SegmentHeader) + sizeof(BlockHeader) * cfg.block_num, 64));

    for (uint32_t i = 0; i < cfg.block_num; ++i) {
      BlockHeader* b = HeaderAt(i);
      b->magic = kBlockMagic;
      b->version = kBlockVersion;
      b->index = i;
      b->lock.store(0, std::memory_order_release);
      b->state.store(static_cast<uint32_t>(BlockState::FREE), std::memory_order_release);
      b->generation = 0;
      b->readers.store(0, std::memory_order_relaxed);
      b->capacity = cfg.block_capacity;
      b->used = 0;
      b->arena_offset = 0;
      b->msg_offset = 0;
      b->seq.store(0, std::memory_order_relaxed);
      b->timestamp_ns = 0;

      uint8_t* chunk = reinterpret_cast<uint8_t*>(mem) + hdr->payload_base +
                       static_cast<size_t>(i) * hdr->chunk_size;
      if (Arena::Create(chunk, hdr->chunk_size) == nullptr) {
        PANGOFLY_ERROR << "[segment] arena create failed for block " << i << std::endl;
        munmap(mem, total);
        base_ = nullptr;
        Region::Release(w);
        window_ = Window{};
        shm_unlink(name.c_str());
        return false;
      }
    }
    // magic 最后写：作为「初始化完成」标志。若在 memset 后立刻写 magic，
    // 另一个进程可能在块头/arena 尚未就绪时就看到了合法 magic 而 attach 上来。
    std::atomic_thread_fence(std::memory_order_release);
    hdr->magic = kSegmentMagic;
  } else {
    // 等待创建者完成初始化（最多 2 秒），避免同时启动时读到半初始化的头
    for (int spin = 0; spin < 2000 && hdr->magic != kSegmentMagic; ++spin) {
      struct timespec req{0, 1000000};   // 1ms
      nanosleep(&req, nullptr);
    }
    if (hdr->magic != kSegmentMagic || hdr->version != kSegmentVersion) {
      PANGOFLY_ERROR << "[segment] header magic/version mismatch" << std::endl;
      munmap(mem, total);
      base_ = nullptr;
      Region::Release(w);
      window_ = Window{};
      return false;
    }
    // 打开者不校验 cfg：它的 block_num / block_capacity 以段头记录为准。
    // 调用方通常只关心 channel 名，要求它重复声明写端配置不现实。
    if (hdr->total_size != total) {
      PANGOFLY_ERROR << "[segment] size mismatch: header=" << hdr->total_size
                     << " actual=" << total << std::endl;
      munmap(mem, total);
      base_ = nullptr;
      Region::Release(w);
      window_ = Window{};
      return false;
    }
    // 地址握手：必须与创建者映射到同一 VA，否则容器内裸指针跨进程失效
    if (hdr->self_base != reinterpret_cast<uint64_t>(mem)) {
      PANGOFLY_ERROR << "[segment] ADDRESS MISMATCH: creator=0x" << std::hex
                     << hdr->self_base << " self=0x" << reinterpret_cast<uintptr_t>(mem)
                     << std::dec << std::endl;
      munmap(mem, total);
      base_ = nullptr;
      Region::Release(w);
      window_ = Window{};
      return false;
    }
  }

  header_ = hdr;
  PANGOFLY_INFO << "[segment] " << (creator ? "created" : "opened")
                << " channel=" << channel << " base=0x" << std::hex
                << reinterpret_cast<uintptr_t>(mem) << std::dec
                << " blocks=" << hdr->block_num << " cap=" << hdr->block_capacity
                << " total=" << total << std::endl;
  return true;
}

// ---------------------------------------------------------------------------
// 写端
// ---------------------------------------------------------------------------
BlockHeader* Segment::TryAcquireWritable() {
  if (header_ == nullptr) return nullptr;

  const uint32_t n = header_->block_num;

  // 1) 优先复用 FREE 块
  for (uint32_t i = 0; i < n; ++i) {
    BlockHeader* b = HeaderAt(i);
    if (b->state.load(std::memory_order_relaxed) !=
        static_cast<uint32_t>(BlockState::FREE)) {
      continue;
    }
    BlockGuard g(b);
    uint32_t expect = static_cast<uint32_t>(BlockState::FREE);
    if (!b->state.compare_exchange_strong(expect,
                                          static_cast<uint32_t>(BlockState::WRITING),
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed)) {
      continue;
    }
    b->generation += 1;
    b->readers = 0;
    b->used = 0;
    b->msg_offset = 0;
    Arena* a = BlockArena(b);
    if (a != nullptr) a->Reset();
    return b;
  }

  // 2) 无空闲块时回收「最旧且无读者」的已发布块（latest-wins，不阻塞写端）
  BlockHeader* victim = nullptr;
  uint64_t victim_seq = 0;
  for (uint32_t i = 0; i < n; ++i) {
    BlockHeader* b = HeaderAt(i);
    if (b->state.load(std::memory_order_relaxed) !=
        static_cast<uint32_t>(BlockState::PUBLISHED)) {
      continue;
    }
    if (b->readers.load(std::memory_order_acquire) != 0) continue;
    const uint64_t s = b->seq.load(std::memory_order_acquire);
    if (victim == nullptr || s < victim_seq) {
      victim = b;
      victim_seq = s;
    }
  }
  if (victim == nullptr) return nullptr;   // 全部在写或被读：背压

  BlockGuard g(victim);
  if (victim->state.load(std::memory_order_acquire) !=
          static_cast<uint32_t>(BlockState::PUBLISHED) ||
      victim->readers.load(std::memory_order_acquire) != 0) {
    return nullptr;   // 挑选期间被他人改动
  }
  victim->state.store(static_cast<uint32_t>(BlockState::WRITING),
                      std::memory_order_release);
  victim->generation += 1;
  victim->readers.store(0, std::memory_order_relaxed);
  victim->used = 0;
  victim->msg_offset = 0;
  Arena* a = BlockArena(victim);
  if (a != nullptr) a->Reset();
  return victim;
}

void Segment::Publish(BlockHeader* b, uint32_t msg_offset, uint64_t ts_ns) {
  if (b == nullptr || header_ == nullptr) return;
  BlockGuard g(b);
  b->msg_offset = msg_offset;
  b->used = static_cast<uint32_t>(BlockArena(b) ? BlockArena(b)->Used() : 0);
  b->readers.store(0, std::memory_order_relaxed);
  b->seq.store(header_->next_seq.fetch_add(1, std::memory_order_acq_rel),
               std::memory_order_relaxed);
  b->timestamp_ns = ts_ns;
  b->state.store(static_cast<uint32_t>(BlockState::PUBLISHED), std::memory_order_release);
}

void Segment::AbortWrite(BlockHeader* b) {
  if (b == nullptr) return;
  BlockGuard g(b);
  Arena* a = BlockArena(b);
  if (a != nullptr) a->Reset();
  b->used = 0;
  b->msg_offset = 0;
  b->readers.store(0, std::memory_order_relaxed);
  b->state.store(static_cast<uint32_t>(BlockState::FREE), std::memory_order_release);
}

// ---------------------------------------------------------------------------
// 读端
// ---------------------------------------------------------------------------
BlockHeader* Segment::AcquireRead(uint64_t* since_seq) {
  if (header_ == nullptr) return nullptr;

  const uint64_t floor = (since_seq != nullptr) ? *since_seq : 0;

  // FIFO：取 seq 最小且大于 floor 的已发布块，保证不丢消息
  BlockHeader* best = nullptr;
  uint64_t best_seq = 0;

  for (uint32_t i = 0; i < header_->block_num; ++i) {
    BlockHeader* b = HeaderAt(i);
    if (b->state.load(std::memory_order_acquire) !=
        static_cast<uint32_t>(BlockState::PUBLISHED)) {
      continue;
    }
    const uint64_t s = b->seq.load(std::memory_order_acquire);
    if (s <= floor) continue;
    if (best == nullptr || s < best_seq) {
      best = b;
      best_seq = s;
    }
  }
  if (best == nullptr) return nullptr;

  {
    BlockGuard g(best);
    // 双重校验：挑选期间可能被写端回收并重发
    if (best->state.load(std::memory_order_acquire) !=
            static_cast<uint32_t>(BlockState::PUBLISHED) ||
        best->seq.load(std::memory_order_relaxed) != best_seq) {
      return nullptr;
    }
    best->readers.fetch_add(1, std::memory_order_acq_rel);
  }

  if (since_seq != nullptr) *since_seq = best_seq;
  return best;
}

// 读者只减少引用计数，绝不回收块。
// 块的回收权完全归写端：只有写端在 TryAcquireWritable 里把 PUBLISHED
// 转为 WRITING 时才 Reset arena。否则「读者归还 → 立即 Reset → 写端
// 正在复用」会互相踩踏。
void Segment::ReleaseRead(BlockHeader* b) {
  if (b == nullptr) return;
  BlockGuard g(b);
  b->readers.fetch_sub(1, std::memory_order_acq_rel);
}

BlockHeader* Segment::WaitRead(uint64_t* since_seq, uint64_t timeout_ns) {
  const uint64_t deadline = NowNs() + timeout_ns;
  while (true) {
    BlockHeader* b = AcquireRead(since_seq);
    if (b != nullptr) return b;
    if (NowNs() >= deadline) return nullptr;
    struct timespec req{0, 100000};   // 100us
    nanosleep(&req, nullptr);
  }
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
void* Segment::BlockPayload(BlockHeader* b) {
  if (b == nullptr || header_ == nullptr) return nullptr;
  return reinterpret_cast<uint8_t*>(base_) + header_->payload_base +
         static_cast<size_t>(b->index) * header_->chunk_size;
}

const void* Segment::BlockPayload(const BlockHeader* b) const {
  return const_cast<Segment*>(this)->BlockPayload(const_cast<BlockHeader*>(b));
}

Arena* Segment::BlockArena(BlockHeader* b) {
  void* payload = BlockPayload(b);
  if (payload == nullptr) return nullptr;
  return Arena::Attach(static_cast<uint8_t*>(payload) + b->arena_offset);
}

const Arena* Segment::BlockArena(const BlockHeader* b) const {
  return const_cast<Segment*>(this)->BlockArena(const_cast<BlockHeader*>(b));
}

void Segment::Dump() const {
  if (header_ == nullptr) {
    std::fprintf(stderr, "[segment] not open\n");
    return;
  }
  std::fprintf(stderr, "[segment] channel=%s base=0x%lx blocks=%u cap=%u chunk=%u\n",
               channel_.c_str(), (unsigned long)reinterpret_cast<uintptr_t>(base_),
               header_->block_num, header_->block_capacity, header_->chunk_size);
  for (uint32_t i = 0; i < header_->block_num; ++i) {
    const BlockHeader* b = HeaderAt(i);
    std::fprintf(stderr, "  block[%u] state=%u gen=%u readers=%u seq=%lu used=%u\n", i,
                 b->state.load(std::memory_order_relaxed), b->generation,
                 b->readers.load(std::memory_order_relaxed),
                 (unsigned long)b->seq.load(std::memory_order_relaxed), b->used);
  }
}

}  // namespace shm
}  // namespace pangofly
