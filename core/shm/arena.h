#ifndef PANGOFLY_V2_ARENA_H_
#define PANGOFLY_V2_ARENA_H_

// ============================================================================
// Arena — 共享内存块内的 TLSF 分配器
//
// 设计要点
//   1. 全部元数据都在 arena 内存内部，不含任何外部指针
//      → 同地址映射后，收发双方看到的 arena 完全一致
//   2. 空闲链表存【相对 arena base 的偏移】，不是绝对指针
//   3. Allocate / Deallocate 均为 O(1)（TLSF 两级位图）
//   4. 相邻空闲块自动合并（boundary tags）
//   5. Reset() 为 O(1)
//   6. 分配失败返回 nullptr，由上层 Allocator 转成 std::bad_alloc
//
// 块布局（size 为 16 的倍数，低 4 位用于标记）
//   offset o      : uint32 size|flags
//   offset o+4    : uint32 prev_raw   (前一个物理块的 raw，用于向后合并)
//   offset o+8    : uint32 next_free  (仅空闲块使用，相对 base 的偏移)
//   offset o+12   : uint32 prev_free  (仅空闲块使用)
//   offset o+16   : payload 起点（保证对齐到 16）
//   紧跟 payload 末尾 8 字节 : uint32 owner_offset, uint32 owner_magic
//   offset o+size-4: uint32 footer_raw (= raw，用于向前合并)
//
// flags 位定义：bit0 = 空闲，bit1 = 已对齐分配（payload 偏移 > 16）
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace pangofly {
namespace shm {

constexpr uint64_t kArenaMagic   = 0x50464152454E4132ULL;  // "PFARENA2"
constexpr uint32_t kArenaVersion = 2;

constexpr size_t kArenaAlign    = 16;
constexpr size_t kBlockOverhead = 16;   // 头 8 + 链接 8（对齐时也被复用）
constexpr size_t kBlockFooter   = 4;    // 尾部 size
constexpr size_t kBlockMin      = 32;
constexpr size_t kOwnerTrailer  = 8;    // payload 前 8 字节记录 owner offset

constexpr size_t kFlCount = 32;
constexpr size_t kSlCount = 16;

constexpr uint32_t kFlagFree    = 0x1;
constexpr uint32_t kFlagAligned = 0x2;
constexpr uint32_t kSizeMask    = ~uint32_t{0xF};
constexpr uint32_t kOwnerMagic  = 0x5046414FU;  // "PFAO"

// 哨兵：偏移 0 是合法的块起点，不能当作「空」使用
constexpr uint32_t kNoOffset = 0xFFFFFFFFu;

class Arena {
 public:
  static Arena* Create(void* mem, size_t bytes) noexcept;
  static Arena* Attach(void* mem) noexcept;

  // 容纳 data_bytes 所需的总字节数（含元数据与对齐余量）
  static size_t RequiredBytes(size_t data_bytes) noexcept {
    return sizeof(Arena) + kArenaAlign * 2 + data_bytes + kBlockMin * 2;
  }

  void*  Allocate(size_t bytes, size_t align = kArenaAlign) noexcept;
  void   Deallocate(void* p, size_t bytes) noexcept;

  void   Reset() noexcept;
  size_t Capacity() const noexcept { return capacity_; }
  size_t Used()     const noexcept { return used_; }
  size_t Peak()     const noexcept { return peak_; }
  size_t FreeBytes() const noexcept { return capacity_ - used_; }
  bool   IsValid()  const noexcept { return magic_ == kArenaMagic; }

  // 供调试：统计空闲块数与最大空闲块
  void Stats(uint32_t* free_blocks, uint32_t* largest_free) const noexcept;

 private:
  Arena() noexcept {
    for (size_t i = 0; i < kFlCount; ++i) {
      sl_bitmap_[i] = 0;
      for (size_t j = 0; j < kSlCount; ++j) {
        free_head_[i][j] = kNoOffset;
      }
    }
  }

  uint8_t*       base()       noexcept {
    return reinterpret_cast<uint8_t*>(this) + data_begin_;
  }
  const uint8_t* base() const noexcept {
    return reinterpret_cast<const uint8_t*>(this) + data_begin_;
  }

  uint32_t* size_slot(uint32_t off)   noexcept { return reinterpret_cast<uint32_t*>(base() + off); }
  uint32_t* prev_slot(uint32_t off)   noexcept { return reinterpret_cast<uint32_t*>(base() + off + 4); }
  uint32_t* nextf_slot(uint32_t off)  noexcept { return reinterpret_cast<uint32_t*>(base() + off + 8); }
  uint32_t* prevf_slot(uint32_t off)  noexcept { return reinterpret_cast<uint32_t*>(base() + off + 12); }
  uint32_t* footer_slot(uint32_t raw_size, uint32_t off) noexcept {
    return reinterpret_cast<uint32_t*>(base() + off + (raw_size & kSizeMask) - kBlockFooter);
  }

  static uint32_t RawOf(uint32_t size, uint32_t flags) noexcept { return size | flags; }
  static uint32_t SizeOf(uint32_t raw) noexcept { return raw & kSizeMask; }
  static bool     IsFreeRaw(uint32_t raw) noexcept { return (raw & kFlagFree) != 0; }

  // 写入块大小并同步 footer / 后继块的 prev
  void WriteRaw(uint32_t off, uint32_t raw) noexcept;

  static uint32_t Normalize(size_t bytes, bool aligned) noexcept {
    size_t need = kBlockOverhead + (aligned ? kArenaAlign : 0) + bytes + kBlockFooter;
    if (need < kBlockMin) need = kBlockMin;
    need = (need + kArenaAlign - 1) & ~(kArenaAlign - 1);
    return static_cast<uint32_t>(need);
  }

  static void Mapping(size_t size, size_t* fl, size_t* sl) noexcept;
  // 在 TLSF 位图中找一个「实际大小 >= need」的空闲块，返回其偏移
  uint32_t FindBlock(size_t need, size_t* out_fl, size_t* out_sl) noexcept;

  void InsertFree(uint32_t off) noexcept;
  void RemoveFree(uint32_t off) noexcept;
  uint32_t MergeWithNext(uint32_t off) noexcept;
  uint32_t MergeWithPrev(uint32_t off) noexcept;

  void SetBitmap(size_t fl, size_t sl) noexcept;
  void ClearBitmap(size_t fl, size_t sl) noexcept;

  uint64_t magic_    = kArenaMagic;
  uint32_t version_  = kArenaVersion;
  uint32_t capacity_ = 0;
  uint32_t used_     = 0;
  uint32_t peak_     = 0;
  uint32_t data_begin_ = 0;   // 数据区相对 this 的偏移（16 对齐）
  uint32_t fl_bitmap_ = 0;
  uint32_t sl_bitmap_[kFlCount];
  uint32_t free_head_[kFlCount][kSlCount];
};

// ---------------------------------------------------------------------------
// 实现
// ---------------------------------------------------------------------------

inline void Arena::WriteRaw(uint32_t off, uint32_t raw) noexcept {
  *size_slot(off) = raw;
  *footer_slot(raw, off) = raw;
  uint32_t next = off + SizeOf(raw);
  if (next < capacity_) {
    *prev_slot(next) = raw;
  }
}

inline void Arena::Mapping(size_t size, size_t* fl, size_t* sl) noexcept {
  size_t r = 0;
  size_t v = size;
  while (v > 1) { v >>= 1; ++r; }
  if (r >= kFlCount) r = kFlCount - 1;
  *fl = r;

  size_t base_pow = size_t{1} << r;
  size_t step = base_pow / kSlCount;
  if (step == 0) step = 1;
  size_t s = (size - base_pow) / step;
  if (s >= kSlCount) s = kSlCount - 1;
  *sl = s;
}

inline void Arena::SetBitmap(size_t fl, size_t sl) noexcept {
  fl_bitmap_ |= (uint32_t{1} << fl);
  sl_bitmap_[fl] |= (uint32_t{1} << sl);
}

inline void Arena::ClearBitmap(size_t fl, size_t sl) noexcept {
  sl_bitmap_[fl] &= ~(uint32_t{1} << sl);
  if (sl_bitmap_[fl] == 0) {
    fl_bitmap_ &= ~(uint32_t{1} << fl);
  }
}

inline void Arena::InsertFree(uint32_t off) noexcept {
  uint32_t size = SizeOf(*size_slot(off));
  size_t fl = 0, sl = 0;
  Mapping(size, &fl, &sl);

  uint32_t head = free_head_[fl][sl];
  *nextf_slot(off) = head;
  *prevf_slot(off) = kNoOffset;
  if (head != kNoOffset) {
    *prevf_slot(head) = off;
  }
  free_head_[fl][sl] = off;
  SetBitmap(fl, sl);
}

inline void Arena::RemoveFree(uint32_t off) noexcept {
  uint32_t size = SizeOf(*size_slot(off));
  size_t fl = 0, sl = 0;
  Mapping(size, &fl, &sl);

  uint32_t nx = *nextf_slot(off);
  uint32_t pv = *prevf_slot(off);
  if (pv != kNoOffset) {
    *nextf_slot(pv) = nx;
  } else {
    free_head_[fl][sl] = nx;
  }
  if (nx != kNoOffset) {
    *prevf_slot(nx) = pv;
  }
  if (free_head_[fl][sl] == kNoOffset) {
    ClearBitmap(fl, sl);
  }
}

inline uint32_t Arena::MergeWithNext(uint32_t off) noexcept {
  uint32_t raw = *size_slot(off);
  uint32_t size = SizeOf(raw);
  uint32_t next = off + size;
  if (next >= capacity_) return off;

  uint32_t next_raw = *size_slot(next);
  if (!IsFreeRaw(next_raw)) return off;

  RemoveFree(next);
  uint32_t merged = size + SizeOf(next_raw);
  WriteRaw(off, RawOf(merged, kFlagFree));
  return off;
}

inline uint32_t Arena::MergeWithPrev(uint32_t off) noexcept {
  uint32_t prev_raw = *prev_slot(off);
  if (prev_raw == 0) return off;
  if (!IsFreeRaw(prev_raw)) return off;

  uint32_t prev_size = SizeOf(prev_raw);
  uint32_t prev_off = off - prev_size;
  RemoveFree(prev_off);

  uint32_t merged = prev_size + SizeOf(*size_slot(off));
  WriteRaw(prev_off, RawOf(merged, kFlagFree));
  return prev_off;
}

// 在指定桶内寻找第一个实际大小 >= need 的空闲块。
// 同一 (fl,sl) 桶内的块大小落在一个区间里，不一定都满足 need，
// 因此必须沿链表逐个检查。
inline uint32_t Arena::FindBlock(size_t need, size_t* out_fl, size_t* out_sl) noexcept {
  size_t fl = 0, sl = 0;
  Mapping(need, &fl, &sl);

  // 1) 当前 fl / sl 起的同位图
  for (size_t f = fl; f < kFlCount; ++f) {
    uint32_t sl_map = sl_bitmap_[f];
    if (f == fl) sl_map &= (~uint32_t{0} << sl);
    if (sl_map == 0) continue;

    for (size_t s = 0; s < kSlCount; ++s) {
      if ((sl_map & (uint32_t{1} << s)) == 0) continue;
      uint32_t off = free_head_[f][s];
      while (off != kNoOffset) {
        uint32_t size = SizeOf(*size_slot(off));
        if (size >= need) {
          *out_fl = f;
          *out_sl = s;
          return off;
        }
        off = *nextf_slot(off);
      }
    }
  }
  return kNoOffset;
}

inline Arena* Arena::Create(void* mem, size_t bytes) noexcept {
  if (mem == nullptr) return nullptr;
  if (bytes < sizeof(Arena) + kBlockMin * 2) return nullptr;

  // 元数据紧贴 mem 起始，数据区从 sizeof(Arena) 开始。
  // 调用方保证 mem 已按 kArenaAlign 对齐（shm 块起始天然页对齐）。
  new (mem) Arena();
  Arena* a = reinterpret_cast<Arena*>(mem);

  // 数据区起点必须是 16 的倍数，向上取整到 kArenaAlign
  uintptr_t data_start = (reinterpret_cast<uintptr_t>(a) + sizeof(Arena) + kArenaAlign - 1)
                         & ~(uintptr_t)(kArenaAlign - 1);
  size_t overhead = data_start - reinterpret_cast<uintptr_t>(a);
  if (bytes <= overhead + kBlockMin) return nullptr;

  size_t data_bytes = (bytes - overhead) & ~(kArenaAlign - 1);
  if (data_bytes < kBlockMin) return nullptr;

  a->data_begin_ = static_cast<uint32_t>(overhead);
  a->capacity_ = static_cast<uint32_t>(data_bytes);

  a->WriteRaw(0, RawOf(static_cast<uint32_t>(data_bytes), kFlagFree));
  *a->prev_slot(0) = 0;
  a->InsertFree(0);
  return a;
}

inline Arena* Arena::Attach(void* mem) noexcept {
  if (mem == nullptr) return nullptr;
  Arena* a = reinterpret_cast<Arena*>(mem);
  if (a->magic_ != kArenaMagic || a->version_ != kArenaVersion) return nullptr;
  return a;
}

inline void* Arena::Allocate(size_t bytes, size_t align) noexcept {
  if (bytes == 0) return nullptr;

  bool aligned = align > kArenaAlign;
  uint32_t need = Normalize(bytes, aligned);
  if (need > capacity_) return nullptr;

  size_t fl = 0, sl = 0;
  uint32_t off = FindBlock(need, &fl, &sl);
  if (off == kNoOffset) return nullptr;

  uint32_t blk_raw = *size_slot(off);
  uint32_t blk_size = SizeOf(blk_raw);
  RemoveFree(off);

  uint32_t remain = blk_size - need;
  if (remain >= kBlockMin) {
    WriteRaw(off, RawOf(need, 0));
    uint32_t rest = off + need;
    WriteRaw(rest, RawOf(remain, kFlagFree));
    InsertFree(rest);
  } else {
    need = blk_size;
    WriteRaw(off, RawOf(blk_size, 0));
  }

  uint32_t payload_off = off + kBlockOverhead;
  uint8_t* payload = base() + payload_off;
  if (aligned) {
    uintptr_t p = reinterpret_cast<uintptr_t>(payload);
    uintptr_t a = (p + align - 1) & ~(uintptr_t)(align - 1);
    if (a + bytes > reinterpret_cast<uintptr_t>(base()) + off + need - kBlockFooter) {
      // 保险：装不下则退回不保证超对齐
      a = p;
    }
    payload = reinterpret_cast<uint8_t*>(a);
  }

  // owner trailer 紧贴 payload 之前 8 字节
  uint32_t* trailer = reinterpret_cast<uint32_t*>(payload - kOwnerTrailer);
  trailer[0] = off;
  trailer[1] = kOwnerMagic;

  used_ += need;
  if (used_ > peak_) peak_ = used_;
  return payload;
}

inline void Arena::Deallocate(void* p, size_t bytes) noexcept {
  (void)bytes;
  if (p == nullptr) return;

  uint8_t* payload = static_cast<uint8_t*>(p);
  uint32_t* trailer = reinterpret_cast<uint32_t*>(payload - kOwnerTrailer);
  if (trailer[1] != kOwnerMagic) return;   // 非本 arena 分配

  uint32_t off = trailer[0];
  if (off >= capacity_) return;

  uint32_t raw = *size_slot(off);
  if (IsFreeRaw(raw)) return;              // double free

  uint32_t size = SizeOf(raw);
  used_ = (used_ >= size) ? (used_ - size) : 0;

  WriteRaw(off, RawOf(size, kFlagFree));
  off = MergeWithPrev(off);
  off = MergeWithNext(off);
  InsertFree(off);
}

inline void Arena::Reset() noexcept {
  used_ = 0;
  peak_ = 0;
  fl_bitmap_ = 0;
  for (size_t i = 0; i < kFlCount; ++i) {
    sl_bitmap_[i] = 0;
    for (size_t j = 0; j < kSlCount; ++j) {
      free_head_[i][j] = kNoOffset;
    }
  }
  if (capacity_ >= kBlockMin) {
    WriteRaw(0, RawOf(capacity_, kFlagFree));
    *prev_slot(0) = 0;
    InsertFree(0);
  }
}

inline void Arena::Stats(uint32_t* free_blocks, uint32_t* largest_free) const noexcept {
  uint32_t count = 0;
  uint32_t largest = 0;
  uint32_t off = 0;
  const uint8_t* b = base();
  while (off < capacity_) {
    uint32_t raw = *reinterpret_cast<const uint32_t*>(b + off);
    uint32_t size = SizeOf(raw);
    if (size == 0) break;
    if (IsFreeRaw(raw)) {
      ++count;
      if (size > largest) largest = size;
    }
    off += size;
  }
  if (free_blocks) *free_blocks = count;
  if (largest_free) *largest_free = largest;
}

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_ARENA_H_
