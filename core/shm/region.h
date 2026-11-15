#ifndef PANGOFLY_V2_REGION_H_
#define PANGOFLY_V2_REGION_H_

// ============================================================================
// Region — 共享内存虚拟地址空间的窗口注册表
//
// 目标：为每个 channel 分配一个【独占、不重叠、固定】的虚拟地址窗口，
//       使得收发双方映射到同一 VA，容器内的裸指针因此天然跨进程有效。
//
// 旧实现的两个缺陷在此彻底消除：
//   1. 步长 2MB < 单通道实际占用 16.78MB → 相邻通道必然重叠
//   2. channel_id % 100 哈希碰撞 → 不同 channel 撞到同一槽位
//
// 新方案：
//   * 注册表放在一个独立的、极小的 shm 中，所有进程看到同一份分配结果
//   * 窗口大小固定（默认 64MB），按需向上取整
//   * 名字完整记录，无哈希碰撞
//   * 映射失败即硬失败，绝不回退到任意地址
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace pangofly {
namespace shm {

// 地址规划按指针宽度分两套：64 位用 48 位地址空间的高位区，32 位只能用
// 用户态低地址区。默认值必须在本平台可用，否则每个应用都得自己写一遍
// RegionConfig 才能启动。
#if UINTPTR_MAX > 0xFFFFFFFFull
struct RegionConfig {
  uintptr_t base = 0x600000000000ULL;   // 预留 VA 起点（48 位地址空间高位）
  size_t total_size = 8ULL << 30;       // 8 GiB
  size_t window_size = 64ULL << 20;     // 64 MiB / channel
  uint32_t max_channels = 128;
  bool require_fixed = true;            // 固定地址是硬前提
  // 注册表自身的固定映射地址，必须落在 base..base+total_size 之外
  uintptr_t registry_base = 0x580000000000ULL;
};
#else
// 32 位（如 RV1106 / armv7l）：48 位地址会被截断成 0，必须全用低地址。
//   预留窗口区 0x40000000 起共 512MB → 结束于 0x60000000
//   注册表     0x70000000（位于窗口区之外）
struct RegionConfig {
  uintptr_t base = 0x40000000ULL;
  size_t total_size = 512ULL << 20;
  size_t window_size = 64ULL << 20;
  uint32_t max_channels = 8;
  bool require_fixed = true;
  uintptr_t registry_base = 0x70000000ULL;
};
#endif

struct Window {
  uintptr_t addr = 0;
  size_t size = 0;
  uint32_t index = 0;
  bool valid() const noexcept { return addr != 0 && size != 0; }
};

// 全局 Region（进程内单例，用显式 Init 而非静态构造）
class Region {
 public:
  // 必须在使用前显式调用；重复调用只生效第一次
  static bool Init(const RegionConfig& cfg = RegionConfig{});

  // 为 channel 独占一个窗口。need_bytes 超过 window_size 时向上取整到
  // window_size 的整数倍并占用多个连续槽位；槽位耗尽返回无效 Window。
  static Window Acquire(const std::string& channel, size_t need_bytes);

  // 释放窗口（最后一个使用者退出时调用）
  static void Release(const Window& w);

  // 该地址是否落在本 Region 的预留范围内
  static bool Contains(const void* p);

  static uintptr_t Base();
  static size_t TotalSize();
  static void Dump();

  // 诊断：返回窗口对应的 channel 名（未注册返回空串）
  static std::string ChannelOf(const Window& w);
};

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_REGION_H_
