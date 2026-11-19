//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <future>
#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

class BufferPoolManager;
class ReadPageGuard;
class WritePageGuard;

/**
 * @brief A helper class for `BufferPoolManager` that manages a frame of memory and related metadata.
 *
 * This class represents headers for frames of memory that the `BufferPoolManager` stores pages of data into. Note that
 * the actual frames of memory are not stored directly inside a `FrameHeader`, rather the `FrameHeader`s store pointer
 * to the frames and are stored separately them.
 *
 * ---
 *
 * Something that may (or may not) be of interest to you is why the field `data_` is stored as a vector that is
 * allocated on the fly instead of as a direct pointer to some pre-allocated chunk of memory.
 *
 * In a traditional production buffer pool manager, all memory that the buffer pool is intended to manage is allocated
 * in one large contiguous array (think of a very large `malloc` call that allocates several gigabytes of memory up
 * front). This large contiguous block of memory is then divided into contiguous frames. In other words, frames are
 * defined by an offset from the base of the array in page-sized (4 KB) intervals.
 *
 * In BusTub, we instead allocate each frame on its own (via a `std::vector<char>`) in order to easily detect buffer
 * overflow with address sanitizer. Since C++ has no notion of memory safety, it would be very easy to cast a page's
 * data pointer into some large data type and start overwriting other pages of data if they were all contiguous.
 *
 * If you would like to attempt to use more efficient data structures for your buffer pool manager, you are free to do
 * so. However, you will likely benefit significantly from detecting buffer overflow in future projects (especially
 * project 2).
 */
/**
 * @brief `FrameHeader` 是 `BufferPoolManager` 管理单个内存帧及其元数据的辅助类。页面的实际字节存放在
 * `data_` 中。生产级缓冲池通常一次分配一大块连续内存，再按页大小切分成帧；BusTub 则为每个帧单独使用
 * `std::vector<char>` 分配内存，以便 AddressSanitizer 更容易发现跨页越界。你可以更换数据结构，但后续项目
 * 尤其是 Project 2 通常会受益于这种越界检测能力。
 */
class FrameHeader {
  friend class BufferPoolManager;
  friend class ReadPageGuard;
  friend class WritePageGuard;

 public:
  explicit FrameHeader(frame_id_t frame_id);

 private:
  auto GetData() const -> const char *;
  auto GetDataMut() -> char *;
  void Reset();

  /** @brief The frame ID / index of the frame this header represents. */
  /** @brief 此 header 所代表的帧 ID／帧下标。 */
  const frame_id_t frame_id_;

  // 当前承载的页面身份，可随 frame 复用而改变；frame_id_ 则始终不变。
  page_id_t page_id_;
  /** @brief The readers / writer latch for this frame. */
  /** @brief 保护该帧页面数据的读写锁。 */
  std::shared_mutex rwlatch_;

  /** @brief The number of pins on this frame keeping the page in memory. */
  /** @brief 该帧的 pin 数；只要大于 0，其中的页面就必须留在内存中。 */
  std::atomic<size_t> pin_count_;

  /** @brief The dirty flag. */
  /** @brief 脏页标志，表示内存内容是否已被修改但尚未写回磁盘。 */
  bool is_dirty_;

  /**
   * @brief A pointer to the data of the page that this frame holds.
   *
   * If the frame does not hold any page data, the frame contains all null bytes.
   */
  /** @brief 该帧所承载页面的数据；若帧未承载页面，则所有字节均为零。 */
  std::vector<char> data_;

  /**
   * TODO(P1): You may add any fields or helper functions under here that you think are necessary.
   *
   * One potential optimization you could make is storing an optional page ID of the page that the `FrameHeader` is
   * currently storing. This might allow you to skip searching for the corresponding (page ID, frame ID) pair somewhere
   * else in the buffer pool manager...
   */
  /**
   * @brief 你可以在此添加必要的字段或辅助函数。例如保存当前页面的可选 Page ID，可能避免在缓冲池的其他
   * 结构中反向查找对应的 `(page_id, frame_id)`。
   */
};

/**
 * @brief The declaration of the `BufferPoolManager` class.
 *
 * As stated in the writeup, the buffer pool is responsible for moving physical pages of data back and forth from
 * buffers in main memory to persistent storage. It also behaves as a cache, keeping frequently used pages in memory for
 * faster access, and evicting unused or cold pages back out to storage.
 *
 * Make sure you read the writeup in its entirety before attempting to implement the buffer pool manager. You also need
 * to have completed the implementation of both the `ArcReplacer` and `DiskManager` classes.
 */
/**
 * @brief `BufferPoolManager` 负责在内存缓冲帧和持久化存储之间来回移动物理页面。它也充当缓存，把常用页面
 * 留在内存以加速访问，并把不活跃页面驱逐回存储。实现前应完整阅读项目说明，并先完成 `ArcReplacer` 和
 * `DiskManager`（本项目实际通过 `DiskScheduler` 使用磁盘管理器）相关部分。
 */
class BufferPoolManager {
 public:
  BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager = nullptr);
  ~BufferPoolManager();

  auto Size() const -> size_t;
  auto NewPage() -> page_id_t;
  auto DeletePage(page_id_t page_id) -> bool;
  auto CheckedWritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown)
      -> std::optional<WritePageGuard>;
  auto CheckedReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> std::optional<ReadPageGuard>;
  auto WritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> WritePageGuard;
  auto ReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> ReadPageGuard;
  auto FlushPageUnsafe(page_id_t page_id) -> bool;
  auto FlushPage(page_id_t page_id) -> bool;
  void FlushAllPagesUnsafe();
  void FlushAllPages();
  auto GetPinCount(page_id_t page_id) -> std::optional<size_t>;

 private:
  auto SingleRequest(bool is_write, frame_id_t frame_id, page_id_t page_id) -> std::future<bool>;
  auto GetOrLoadPage(page_id_t page_id, AccessType access_type) -> std::optional<frame_id_t>;
  /** @brief The number of frames in the buffer pool. */
  /** @brief 缓冲池中的帧总数。 */
  const size_t num_frames_;

  /** @brief The next page ID to be allocated.  */
  /** @brief 下一个待分配的 Page ID。 */
  std::atomic<page_id_t> next_page_id_;

  /**
   * @brief The latch protecting the buffer pool's inner data structures.
   *
   * TODO(P1) We recommend replacing this comment with details about what this latch actually protects.
   */
  /** @brief 保护缓冲池内部数据结构的锁；实现后应在注释中明确它具体保护哪些状态。 */
  std::shared_ptr<std::mutex> bpm_latch_;

  /** @brief The frame headers of the frames that this buffer pool manages. */
  /** @brief 缓冲池所管理的全部帧头。 */
  std::vector<std::shared_ptr<FrameHeader>> frames_;

  /** @brief The page table that keeps track of the mapping between pages and buffer pool frames. */
  /** @brief 记录 Page ID 到缓冲池 Frame ID 映射的页表。 */
  std::unordered_map<page_id_t, frame_id_t> page_table_;

  /** @brief A list of free frames that do not hold any page's data. */
  /** @brief 尚未承载任何页面数据的空闲帧列表。 */
  std::list<frame_id_t> free_frames_;

  /** @brief The replacer to find unpinned / candidate pages for eviction. */
  /** @brief 用于寻找未被 pin、可作为驱逐候选的帧的替换器。 */
  std::shared_ptr<ArcReplacer> replacer_;

  /** @brief A pointer to the disk scheduler. Shared with the page guards for flushing. */
  /** @brief 磁盘调度器；与 Page Guard 共享，用于刷新页面。 */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief A pointer to the log manager.
   *
   * Note: Please ignore this for P1.
   */
  /** @brief 日志管理器指针；Project 1 中忽略。 */
  LogManager *log_manager_ __attribute__((__unused__));

  /**
   * TODO(P1): You may add additional private members and helper functions if you find them necessary.
   *
   * There will likely be a lot of code duplication between the different modes of accessing a page.
   *
   * We would recommend implementing a helper function that returns the ID of a frame that is free and has nothing
   * stored inside of it. Additionally, you may also want to implement a helper function that returns either a shared
   * pointer to a `FrameHeader` that already has a page's data stored inside of it, or an index to said `FrameHeader`.
   */
  /**
   * @brief 可按需添加私有成员和辅助函数。不同页面访问模式之间可能有大量重复代码；建议抽取“取得一个完全
   * 空闲帧”以及“找到已经保存目标页面的 `FrameHeader`（或其下标）”等辅助逻辑。
   */
};
}  // namespace bustub
