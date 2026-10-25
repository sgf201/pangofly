#include "core/shm/region.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/common/log.h"

namespace pangofly {
namespace shm {
namespace {

constexpr uint64_t kRegistryMagic = 0x5046524547494F4EULL;  // "PFREGION"
constexpr uint32_t kRegistryVersion = 2;
constexpr size_t kNameMax = 64;
constexpr const char* kRegistryName = "/pangofly_v2_registry";
// 注册表自身的固定映射地址，远离数据窗口区（默认 0x600000000000 起）
constexpr uintptr_t kRegistryAddr = 0x580000000000ULL;

enum SlotState : uint32_t {
  SLOT_FREE = 0,
  SLOT_ACTIVE = 1,
};

struct ChannelSlot {
  char name[kNameMax];
  std::atomic<uint32_t> state;
  uint32_t window_index;
  uint32_t span;            // 占用的连续窗口数
  uint64_t mapped_size;
  std::atomic<uint32_t> refcount;
  uint32_t reserved;
};

struct Registry {
  uint64_t magic;
  uint32_t version;
  uint32_t max_channels;
  uint64_t base;
  uint64_t window_size;
  uint64_t total_size;
  uint32_t slot_count;
  uint32_t reserved;
  ChannelSlot slots[1];   // 变长
};

struct RegionState {
  bool inited = false;
  RegionConfig cfg;
  Registry* reg = nullptr;
  size_t reg_bytes = 0;
  std::mutex mu;
};

RegionState& State() {
  static RegionState s;
  return s;
}

size_t RegistryBytes(const RegionConfig& cfg) {
  return sizeof(Registry) + sizeof(ChannelSlot) * (cfg.max_channels - 1);
}

// ---------------------------------------------------------------------------
// 映射辅助：固定地址，失败即返回 nullptr，绝不回退
// ---------------------------------------------------------------------------
void* MapFixedCreate(int fd, size_t size, uintptr_t addr) {
#if defined(MAP_FIXED_NOREPLACE)
  // 先探测该区间是否空闲（Linux 4.17+）
  void* probe = mmap(reinterpret_cast<void*>(addr), size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (probe == MAP_FAILED) {
    PANGOFLY_ERROR << "[region] VA range already occupied: 0x" << std::hex << addr
                   << std::dec << " size=" << size << std::endl;
    return nullptr;
  }
  munmap(probe, size);
#endif

  void* p = mmap(reinterpret_cast<void*>(addr), size, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fd, 0);
  if (p == MAP_FAILED) {
    PANGOFLY_ERROR << "[region] mmap MAP_FIXED failed at 0x" << std::hex << addr
                   << std::dec << std::endl;
    return nullptr;
  }
  if (reinterpret_cast<uintptr_t>(p) != addr) {
    PANGOFLY_ERROR << "[region] mmap returned unexpected address 0x" << std::hex
                   << reinterpret_cast<uintptr_t>(p) << " expected 0x" << addr
                   << std::dec << std::endl;
    munmap(p, size);
    return nullptr;
  }
  return p;
}

}  // namespace

bool Region::Init(const RegionConfig& cfg) {
  RegionState& st = State();
  std::lock_guard<std::mutex> lk(st.mu);
  if (st.inited) return true;

  if (cfg.window_size == 0 || cfg.max_channels == 0) {
    PANGOFLY_ERROR << "[region] invalid config" << std::endl;
    return false;
  }

  const size_t bytes = RegistryBytes(cfg);

  int fd = shm_open(kRegistryName, O_RDWR | O_CREAT | O_EXCL, 0600);
  bool creator = (fd >= 0);
  if (!creator) {
    if (errno != EEXIST) {
      PANGOFLY_ERROR << "[region] shm_open failed" << std::endl;
      return false;
    }
    fd = shm_open(kRegistryName, O_RDWR, 0600);
    if (fd < 0) {
      PANGOFLY_ERROR << "[region] shm_open(existing) failed" << std::endl;
      return false;
    }
  }

  if (creator && ftruncate(fd, static_cast<off_t>(bytes)) < 0) {
    PANGOFLY_ERROR << "[region] ftruncate failed" << std::endl;
    close(fd);
    shm_unlink(kRegistryName);
    return false;
  }

  // 注册表放在预留窗口区之外的一个独立固定地址，
  // 避免与任何数据窗口争用同一段 VA。
  const uintptr_t reg_addr = kRegistryAddr;

  void* reg_mem = MapFixedCreate(fd, bytes, reg_addr);
  if (reg_mem == nullptr) {
    close(fd);
    if (creator) shm_unlink(kRegistryName);
    return false;
  }

  Registry* reg = static_cast<Registry*>(reg_mem);
  if (creator) {
    // Registry 含 std::atomic 成员，用逐字段初始化而不是 memset
    reg->magic = kRegistryMagic;
    reg->version = kRegistryVersion;
    reg->max_channels = cfg.max_channels;
    reg->base = cfg.base;
    reg->window_size = cfg.window_size;
    reg->total_size = cfg.total_size;
    reg->slot_count = cfg.max_channels;
    reg->reserved = 0;
    for (uint32_t i = 0; i < cfg.max_channels; ++i) {
      ChannelSlot& s = reg->slots[i];
      s.name[0] = '\0';
      s.state.store(SLOT_FREE, std::memory_order_release);
      s.window_index = 0;
      s.span = 0;
      s.mapped_size = 0;
      s.refcount.store(0, std::memory_order_release);
      s.reserved = 0;
    }
  } else {
    if (reg->magic != kRegistryMagic || reg->version != kRegistryVersion) {
      PANGOFLY_ERROR << "[region] registry magic/version mismatch" << std::endl;
      close(fd);
      return false;
    }
  }

  close(fd);

  st.cfg = cfg;
  st.cfg.base = reg->base;
  st.cfg.window_size = reg->window_size;
  st.cfg.total_size = reg->total_size;
  st.cfg.max_channels = reg->max_channels;
  st.reg = reg;
  st.reg_bytes = bytes;
  st.inited = true;

  PANGOFLY_INFO << "[region] initialized base=0x" << std::hex << reg->base
                << " window=0x" << reg->window_size << " total=0x" << reg->total_size
                << std::dec << " (registry@" << reg_mem << ")" << std::endl;
  return true;
}

Window Region::Acquire(const std::string& channel, size_t need_bytes) {
  RegionState& st = State();
  std::lock_guard<std::mutex> lk(st.mu);
  if (!st.inited) return {};

  Registry* reg = st.reg;
  const size_t wsize = static_cast<size_t>(reg->window_size);
  const uint32_t max_slots = reg->max_channels;

  // 需要的连续窗口数
  uint32_t span = static_cast<uint32_t>((need_bytes + wsize - 1) / wsize);
  if (span == 0) span = 1;
  if (span > max_slots) {
    PANGOFLY_ERROR << "[region] request too large: " << need_bytes << std::endl;
    return {};
  }

  // 已存在同名 channel：复用并增加引用
  for (uint32_t i = 0; i < max_slots; ++i) {
    ChannelSlot& s = reg->slots[i];
    if (s.state.load(std::memory_order_acquire) != SLOT_ACTIVE) continue;
    if (std::strncmp(s.name, channel.c_str(), kNameMax - 1) != 0) continue;
    s.refcount.fetch_add(1, std::memory_order_acq_rel);
    Window w;
    w.index = s.window_index;
    w.size = s.mapped_size;
    w.addr = reg->base + static_cast<uintptr_t>(s.window_index) * wsize;
    return w;
  }

  // 找一段够长的连续空槽
  for (uint32_t start = 0; start + span <= max_slots; ++start) {
    bool ok = true;
    for (uint32_t k = 0; k < span; ++k) {
      if (reg->slots[start + k].state.load(std::memory_order_acquire) != SLOT_FREE) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;

    ChannelSlot& s = reg->slots[start];
    uint32_t expected = SLOT_FREE;
    if (!s.state.compare_exchange_strong(expected, SLOT_ACTIVE,
                                        std::memory_order_acq_rel)) {
      continue;
    }
    std::strncpy(s.name, channel.c_str(), kNameMax - 1);
    s.name[kNameMax - 1] = '\0';
    s.window_index = start;
    s.span = span;
    s.mapped_size = static_cast<uint64_t>(span) * wsize;
    s.refcount.store(1, std::memory_order_release);

    // 占位标记，防止他人再次分配到这段
    for (uint32_t k = 1; k < span; ++k) {
      ChannelSlot& cont = reg->slots[start + k];
      cont.state.store(SLOT_ACTIVE, std::memory_order_release);
      cont.window_index = start;
      cont.span = 0;   // 0 表示「被前驱占用的延续槽」
      cont.name[0] = '\0';
    }

    Window w;
    w.index = start;
    w.size = static_cast<size_t>(s.mapped_size);
    w.addr = reg->base + static_cast<uintptr_t>(start) * wsize;
    return w;
  }

  PANGOFLY_ERROR << "[region] no free window for channel " << channel
                 << " (need " << span << " slots)" << std::endl;
  return {};
}

void Region::Release(const Window& w) {
  RegionState& st = State();
  std::lock_guard<std::mutex> lk(st.mu);
  if (!st.inited || !w.valid()) return;

  Registry* reg = st.reg;
  if (w.index >= reg->max_channels) return;

  ChannelSlot& s = reg->slots[w.index];
  if (s.state.load(std::memory_order_acquire) != SLOT_ACTIVE) return;
  if (s.span == 0) return;   // 延续槽，由首槽负责释放

  uint32_t rc = s.refcount.fetch_sub(1, std::memory_order_acq_rel);
  if (rc > 1) return;

  uint32_t span = s.span;
  s.name[0] = '\0';
  s.span = 0;
  s.mapped_size = 0;
  s.state.store(SLOT_FREE, std::memory_order_release);

  for (uint32_t k = 1; k < span; ++k) {
    ChannelSlot& cont = reg->slots[w.index + k];
    cont.state.store(SLOT_FREE, std::memory_order_release);
    cont.span = 0;
    cont.name[0] = '\0';
  }
}

bool Region::Contains(const void* p) {
  RegionState& st = State();
  if (!st.inited || p == nullptr) return false;
  uintptr_t a = reinterpret_cast<uintptr_t>(p);
  uintptr_t b = st.reg->base;
  return a >= b && a < b + st.reg->total_size;
}

uintptr_t Region::Base() {
  RegionState& st = State();
  return st.inited ? static_cast<uintptr_t>(st.reg->base) : 0;
}

size_t Region::TotalSize() {
  RegionState& st = State();
  return st.inited ? static_cast<size_t>(st.reg->total_size) : 0;
}

void Region::Dump() {
  RegionState& st = State();
  if (!st.inited) {
    std::fprintf(stderr, "[region] not initialized\n");
    return;
  }
  Registry* reg = st.reg;
  std::fprintf(stderr, "[region] base=0x%lx window=0x%lx total=0x%lx slots=%u\n",
               (unsigned long)reg->base, (unsigned long)reg->window_size,
               (unsigned long)reg->total_size, reg->max_channels);
  for (uint32_t i = 0; i < reg->max_channels; ++i) {
    const ChannelSlot& s = reg->slots[i];
    if (s.state.load(std::memory_order_acquire) != SLOT_ACTIVE) continue;
    if (s.span == 0) continue;
    std::fprintf(stderr, "  [%3u] %-32s addr=0x%lx size=%lu span=%u refs=%u\n", i,
                 s.name,
                 (unsigned long)(reg->base + static_cast<uintptr_t>(i) * reg->window_size),
                 (unsigned long)s.mapped_size, s.span,
                 s.refcount.load(std::memory_order_acquire));
  }
}

std::string Region::ChannelOf(const Window& w) {
  RegionState& st = State();
  if (!st.inited || !w.valid() || w.index >= st.reg->max_channels) return {};
  const ChannelSlot& s = st.reg->slots[w.index];
  if (s.state.load(std::memory_order_acquire) != SLOT_ACTIVE) return {};
  return std::string(s.name);
}

}  // namespace shm
}  // namespace pangofly
