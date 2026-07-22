//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.h
//
// Identification: src/include/storage/page/page_guard.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <future>
#include <memory>

#include "buffer/arc_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"

namespace bustub {

class BufferPoolManager;
class FrameHeader;

/**
 * @brief An RAII object that grants thread-safe read access to a page of data.
 *
 * The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
 * `ReadPageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
 *
 * With `ReadPageGuard`s, there can be multiple threads that share read access to a page's data. However, the existence
 * of any `ReadPageGuard` on a page implies that no thread can be mutating the page's data.
 */
/**
 * @brief `ReadPageGuard` 是授予页面线程安全只读访问权的 RAII 对象。BusTub 只能通过 Page Guard 操作缓冲池
 * 页面数据，因此系统无需手工加锁和解锁。多个线程可以同时持有同一页面的读 Guard，但只要存在任一读
 * Guard，就不能有线程修改该页面。
 */
class ReadPageGuard {
  /** @brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` */
  /** @brief 只有缓冲池管理器可以构造有效的 `ReadPageGuard`。 */
  friend class BufferPoolManager;

 public:
  /**
   * @brief The default constructor for a `ReadPageGuard`.
   *
   * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
   * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
   * via `=`.
   *
   * **Use of an uninitialized page guard is undefined behavior.**
   *
   * In other words, the only way to get a valid `ReadPageGuard` is through the buffer pool manager.
   */
  /**
   * @brief 默认构造的读 Guard 不能直接使用。提供默认构造函数只是为了先在栈上放置一个“未初始化”Guard，
   * 之后再通过移动赋值写入有效 Guard。使用未初始化 Guard 属于未定义行为；有效 Guard 只能由缓冲池管理器
   * 创建。
   */
  ReadPageGuard() = default;

  ReadPageGuard(const ReadPageGuard &) = delete;
  auto operator=(const ReadPageGuard &) -> ReadPageGuard & = delete;
  ReadPageGuard(ReadPageGuard &&that) noexcept;
  auto operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard &;
  auto GetPageId() const -> page_id_t;
  auto GetData() const -> const char *;
  template <class T>
  auto As() const -> const T * {
    return reinterpret_cast<const T *>(GetData());
  }
  auto IsDirty() const -> bool;
  void Flush();
  void Drop();
  ~ReadPageGuard();

 private:
  auto SingleRequest(bool is_write) -> std::future<bool>;
  /** @brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` */
  /** @brief 只有缓冲池管理器可以调用此构造函数创建有效 Guard。 */
  explicit ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame, std::shared_ptr<ArcReplacer> replacer,
                         std::shared_ptr<std::mutex> bpm_latch, std::shared_ptr<DiskScheduler> disk_scheduler);

  /** @brief The page ID of the page we are guarding. */
  /** @brief 当前 Guard 所保护页面的 Page ID。 */
  page_id_t page_id_;

  /**
   * @brief The frame that holds the page this guard is protecting.
   *
   * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
   */
  /** @brief 承载受保护页面的帧；Page Guard 的绝大多数操作应通过这个 `FrameHeader` 共享指针完成。 */
  std::shared_ptr<FrameHeader> frame_;

  /**
   * @brief A shared pointer to the buffer pool's replacer.
   *
   * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's replacer in order to set the frame as evictable on destruction.
   */
  /** @brief 缓冲池替换器的共享指针；Guard 析构时用它把相应帧设置为可驱逐。 */
  std::shared_ptr<ArcReplacer> replacer_;

  /**
   * @brief A shared pointer to the buffer pool's latch.
   *
   * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
   */
  /** @brief 缓冲池锁的共享指针；更新帧在替换器中的驱逐状态时用于保护缓冲池状态。 */
  std::shared_ptr<std::mutex> bpm_latch_;

  /**
   * @brief A shared pointer to the buffer pool's disk scheduler.
   *
   * Used when flushing pages to disk.
   */
  /** @brief 缓冲池磁盘调度器的共享指针，用于把页面刷新到磁盘。 */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief The validity flag for this `ReadPageGuard`.
   *
   * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
   * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
   * automatically set this field to `false`.
   *
   * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
   * or `Drop()` invalid members, causing a segmentation fault.
   */
  /**
   * @brief Guard 有效性标志。默认构造将其设为 `false`，用于防止移动操作、析构或 `Drop()` 对无效成员重复
   * 释放并造成段错误。
   */
  bool is_valid_{false};

  /**
   * TODO(P1): You may add any fields under here that you think are necessary.
   *
   * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
   * `std::shared_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
   */
  /** @brief 可添加必要字段；也可研究 `std::shared_lock`，用 RAII 代替手工 `lock`／`unlock`。 */
};

/**
 * @brief An RAII object that grants thread-safe write access to a page of data.
 *
 * The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
 * `WritePageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
 *
 * With a `WritePageGuard`, there can be only be one thread that has exclusive ownership over the page's data. This
 * means that the owner of the `WritePageGuard` can mutate the page's data as much as they want. However, the existence
 * of a `WritePageGuard` implies that no other `WritePageGuard` or any `ReadPageGuard`s for the same page can exist at
 * the same time.
 */
/**
 * @brief `WritePageGuard` 是授予页面线程安全写访问权的 RAII 对象。写 Guard 对页面拥有独占权，持有者可以
 * 修改页面；同一时刻同一页面不能再存在其他写 Guard 或任何读 Guard。
 */
class WritePageGuard {
  /** @brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` */
  /** @brief 只有缓冲池管理器可以构造有效的 `WritePageGuard`。 */
  friend class BufferPoolManager;

 public:
  /**
   * @brief The default constructor for a `WritePageGuard`.
   *
   * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
   * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
   * via `=`.
   *
   * **Use of an uninitialized page guard is undefined behavior.**
   *
   * In other words, the only way to get a valid `WritePageGuard` is through the buffer pool manager.
   */
  /**
   * @brief 默认构造的写 Guard 不能直接使用，仅用于创建“未初始化”Guard 后再移动赋值。使用未初始化 Guard
   * 属于未定义行为；有效 Guard 只能由缓冲池管理器创建。
   */
  WritePageGuard() = default;

  WritePageGuard(const WritePageGuard &) = delete;
  auto operator=(const WritePageGuard &) -> WritePageGuard & = delete;
  WritePageGuard(WritePageGuard &&that) noexcept;
  auto operator=(WritePageGuard &&that) noexcept -> WritePageGuard &;
  auto GetPageId() const -> page_id_t;
  auto GetData() const -> const char *;
  template <class T>
  auto As() const -> const T * {
    return reinterpret_cast<const T *>(GetData());
  }
  auto GetDataMut() -> char *;
  template <class T>
  auto AsMut() -> T * {
    return reinterpret_cast<T *>(GetDataMut());
  }
  auto IsDirty() const -> bool;
  void Flush();
  void Drop();
  ~WritePageGuard();

 private:
  auto SingleRequest(bool is_write) -> std::future<bool>;
  /** @brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` */
  /** @brief 只有缓冲池管理器可以调用此构造函数创建有效 Guard。 */
  explicit WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame, std::shared_ptr<ArcReplacer> replacer,
                          std::shared_ptr<std::mutex> bpm_latch, std::shared_ptr<DiskScheduler> disk_scheduler);

  /** @brief The page ID of the page we are guarding. */
  /** @brief 当前 Guard 所保护页面的 Page ID。 */
  page_id_t page_id_;

  /**
   * @brief The frame that holds the page this guard is protecting.
   *
   * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
   */
  /** @brief 承载受保护页面的帧；Page Guard 的绝大多数操作应通过这个 `FrameHeader` 共享指针完成。 */
  std::shared_ptr<FrameHeader> frame_;

  /**
   * @brief A shared pointer to the buffer pool's replacer.
   *
   * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's replacer in order to set the frame as evictable on destruction.
   */
  /** @brief 缓冲池替换器的共享指针；Guard 析构时用它把相应帧设置为可驱逐。 */
  std::shared_ptr<ArcReplacer> replacer_;

  /**
   * @brief A shared pointer to the buffer pool's latch.
   *
   * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
   */
  /** @brief 缓冲池锁的共享指针；更新帧在替换器中的驱逐状态时用于保护缓冲池状态。 */
  std::shared_ptr<std::mutex> bpm_latch_;

  /**
   * @brief A shared pointer to the buffer pool's disk scheduler.
   *
   * Used when flushing pages to disk.
   */
  /** @brief 缓冲池磁盘调度器的共享指针，用于把页面刷新到磁盘。 */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief The validity flag for this `WritePageGuard`.
   *
   * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
   * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
   * automatically set this field to `false`.
   *
   * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
   * or `Drop()` invalid members, causing a segmentation fault.
   */
  /** @brief Guard 有效性标志，用于防止移动、析构或 `Drop()` 对无效成员重复释放。 */
  bool is_valid_{false};

  /**
   * TODO(P1): You may add any fields under here that you think are necessary.
   *
   * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
   * `std::unique_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
   */
  /** @brief 可添加必要字段；也可研究 `std::unique_lock`，用 RAII 代替手工 `lock`／`unlock`。 */
};

}  // namespace bustub
