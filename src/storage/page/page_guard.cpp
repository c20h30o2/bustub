//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <cassert>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>
#include "buffer/arc_replacer.h"
#include "common/macros.h"
#include "fmt/chrono.h"
#include "storage/disk/disk_scheduler.h"

namespace bustub {

/**
 * @brief The only constructor for an RAII `ReadPageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to read.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 * @param disk_scheduler A shared pointer to the buffer pool manager's disk scheduler.
 */
/**
 * @brief 唯一能够创建有效 RAII `ReadPageGuard` 的构造函数。
 *
 * 只有缓冲池管理器可以调用此构造函数。
 *
 * TODO(P1)：补充实现。
 *
 * @param page_id 要读取的页面的 Page ID。
 * @param frame 指向承载目标页面的帧的共享指针。
 * @param replacer 指向缓冲池管理器替换器的共享指针。
 * @param bpm_latch 指向缓冲池管理器锁的共享指针。
 * @param disk_scheduler 指向缓冲池管理器磁盘调度器的共享指针。
 */
ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                             std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  frame_->rwlatch_.lock_shared();
  is_valid_ = true;
  // std::cout << std::this_thread::get_id() << " read guard construct success" << '\n';
}

/**
 * @brief The move constructor for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 */
/**
 * @brief `ReadPageGuard` 的移动构造函数。
 *
 * 应先理解移动语义；必须转移至少五个字段，并使源 Guard 失效，否则可能发生资源被重复释放。
 *
 * TODO(P1)：补充实现。
 *
 * @param that 另一个 Page Guard。
 */
ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      is_valid_(std::exchange(that.is_valid_, false)) {
  // std::cout << std::this_thread::get_id() << "read guard move construct success" << '\n';
}

/**
 * @brief The move assignment operator for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
 * holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return ReadPageGuard& The newly valid `ReadPageGuard`.
 */
/**
 * @brief `ReadPageGuard` 的移动赋值运算符。
 *
 * 除转移至少五个字段并使源 Guard 失效外，还必须先释放当前对象可能持有的资源。
 *
 * TODO(P1)：补充实现。
 *
 * @param that 另一个 Page Guard。
 * @return ReadPageGuard& 移动后变为有效状态的当前 `ReadPageGuard`。
 */
auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (is_valid_ && page_id_ == that.page_id_) {
    return *this;
  }
  if (is_valid_ && page_id_ != that.page_id_) {
    frame_->rwlatch_.unlock_shared();
    std::lock_guard<std::mutex> lock(*bpm_latch_);
    if (frame_->pin_count_.fetch_sub(1) == 1) {
      replacer_->SetEvictable(frame_->frame_id_, true);
    }
  }
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = std::exchange(that.is_valid_, false);
  // std::cout << std::this_thread::get_id() << "read guard operator= success" << '\n';

  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
/** @brief 取得当前 Guard 所保护页面的 Page ID。 */
auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid page guard");
  return frame_->page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
/** @brief 取得指向当前 Guard 所保护页面数据的只读指针。 */
auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid page guard");
  return frame_->GetData();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
/** @brief 返回页面是否为脏页，即是否已修改但尚未刷新到磁盘。 */
auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid page guard");
  return frame_->is_dirty_;
}

/**
 * @brief Flushes this page's data safely to disk.
 *
 * TODO(P1): Add implementation.
 */
/** @brief 安全地把该页面的数据刷新到磁盘。TODO(P1)：补充实现。 */
auto ReadPageGuard::SingleRequest(bool is_write) -> std::future<bool> {
  std::promise<bool> promise = disk_scheduler_->CreatePromise();
  std::future<bool> future = promise.get_future();
  std::vector<DiskRequest> reqs;
  reqs.emplace_back(DiskRequest{is_write, frame_->GetDataMut(), frame_->page_id_, std::move(promise)});
  disk_scheduler_->Schedule(reqs);
  return future;
}
void ReadPageGuard::Flush() {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid page guard");
  SingleRequest(true).get();
}

/**
 * @brief Manually drops a valid `ReadPageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 */
/**
 * @brief 手工释放有效 `ReadPageGuard` 的资源；Guard 无效时不做任何事。必须避免重复释放，并仔细考虑所拥有
 * 的资源及其释放顺序；顺序错误很可能导致后续测试失败。在特定情况下可能还需要获取缓冲池管理器的锁。
 * TODO(P1)：补充实现。
 */
void ReadPageGuard::Drop() {
  if (!is_valid_) {
    return;
  }
  frame_->rwlatch_.unlock_shared();
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (frame_->pin_count_.fetch_sub(1) == 1) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  is_valid_ = false;
  replacer_ = nullptr;
  disk_scheduler_ = nullptr;
  bpm_latch_ = nullptr;
  frame_ = nullptr;
}

/** @brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. */
/** @brief `ReadPageGuard` 析构函数，仅调用 `Drop()`。 */
ReadPageGuard::~ReadPageGuard() {
  Drop();
  // std::cout << std::this_thread::get_id() << "read guard distory success" << '\n';
}

/**********************************************************************************************************************/
/**********************************************************************************************************************/
/**********************************************************************************************************************/

/**
 * @brief The only constructor for an RAII `WritePageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to write to.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 * @param disk_scheduler A shared pointer to the buffer pool manager's disk scheduler.
 */
/**
 * @brief 唯一能够创建有效 RAII `WritePageGuard` 的构造函数。
 *
 * 只有缓冲池管理器可以调用此构造函数。
 *
 * TODO(P1)：补充实现。
 *
 * @param page_id 要写入的页面的 Page ID。
 * @param frame 指向承载目标页面的帧的共享指针。
 * @param replacer 指向缓冲池管理器替换器的共享指针。
 * @param bpm_latch 指向缓冲池管理器锁的共享指针。
 * @param disk_scheduler 指向缓冲池管理器磁盘调度器的共享指针。
 */
WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  frame_->rwlatch_.lock();
  // std::cout << "11" << '\n';
  is_valid_ = true;
  // std::cout << std::this_thread::get_id() << "write guard construct success" << '\n';
}

/**
 * @brief The move constructor for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are
 * many great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 */
/**
 * @brief `WritePageGuard` 的移动构造函数。
 *
 * 应先理解移动语义；必须转移至少五个字段，并使源 Guard 失效，否则可能发生资源被重复释放。
 *
 * TODO(P1)：补充实现。
 *
 * @param that 另一个 Page Guard。
 */
WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      is_valid_(std::exchange(that.is_valid_, false)) {
  // std::cout << std::this_thread::get_id() << "write guard move construct success" << '\n';
}

/**
 * @brief The move assignment operator for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are
 * many great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might
 * be holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return WritePageGuard& The newly valid `WritePageGuard`.
 */
/**
 * @brief `WritePageGuard` 的移动赋值运算符。
 *
 * 除转移至少五个字段并使源 Guard 失效外，还必须先释放当前对象可能持有的资源。
 *
 * TODO(P1)：补充实现。
 *
 * @param that 另一个 Page Guard。
 * @return WritePageGuard& 移动后变为有效状态的当前 `WritePageGuard`。
 */
auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (is_valid_ && page_id_ == that.page_id_) {
    return *this;
  }
  if (is_valid_ && page_id_ != that.page_id_) {
    frame_->rwlatch_.unlock();
    std::lock_guard<std::mutex> lock(*bpm_latch_);
    if (frame_->pin_count_.fetch_sub(1) == 1) {
      replacer_->SetEvictable(frame_->frame_id_, true);
    }
  }
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = std::exchange(that.is_valid_, false);
  // std::cout << std::this_thread::get_id() << "write guard operator= success" << '\n';
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
/** @brief 取得当前 Guard 所保护页面的 Page ID。 */
auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
/** @brief 取得指向当前 Guard 所保护页面数据的只读指针。 */
auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

/**
 * @brief Gets a mutable pointer to the page of data this guard is protecting.
 */
/** @brief 取得指向当前 Guard 所保护页面数据的可写指针。 */
auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  frame_->is_dirty_ = true;
  return frame_->GetDataMut();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
/** @brief 返回页面是否为脏页，即是否已修改但尚未刷新到磁盘。 */
auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

auto WritePageGuard::SingleRequest(bool is_write) -> std::future<bool> {
  std::promise<bool> promise = disk_scheduler_->CreatePromise();
  std::future<bool> future = promise.get_future();
  std::vector<DiskRequest> reqs;
  reqs.emplace_back(DiskRequest{is_write, frame_->GetDataMut(), frame_->page_id_, std::move(promise)});
  disk_scheduler_->Schedule(reqs);
  return future;
}
/**
 * @brief Flushes this page's data safely to disk.
 *
 * TODO(P1): Add implementation.
 */
/** @brief 安全地把该页面的数据刷新到磁盘。TODO(P1)：补充实现。 */
void WritePageGuard::Flush() {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid page guard");
  // writepageguard hold uniquelock,always safe to flush
  SingleRequest(true).get();
}

/**
 * @brief Manually drops a valid `WritePageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 */
/**
 * @brief 手工释放有效 `WritePageGuard` 的资源；Guard 无效时不做任何事。必须避免重复释放，并仔细考虑所拥有
 * 的资源及其释放顺序；顺序错误很可能导致后续测试失败。在特定情况下可能还需要获取缓冲池管理器的锁。
 * TODO(P1)：补充实现。
 */
void WritePageGuard::Drop() {
  if (!is_valid_) {
    return;
  }
  frame_->rwlatch_.unlock();
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (frame_->pin_count_.fetch_sub(1) == 1) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  is_valid_ = false;
  replacer_ = nullptr;
  disk_scheduler_ = nullptr;
  bpm_latch_ = nullptr;
  frame_ = nullptr;
}

/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
/** @brief `WritePageGuard` 析构函数，仅调用 `Drop()`。 */
WritePageGuard::~WritePageGuard() {
  Drop();
  // std::cout << std::this_thread::get_id() << "write guard distory success" << '\n';
}

}  // namespace bustub
