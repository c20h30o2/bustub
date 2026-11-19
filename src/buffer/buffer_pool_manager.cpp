//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>
#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page_guard.h"

namespace bustub {

// 复习主线：FrameHeader/构造函数 → NewPage → SingleRequest → GetOrLoadPage → Checked*Page。
// 然后跳到 page_guard.cpp 看页面使用和释放，最后回来读 DeletePage、Flush*、GetPinCount。
// page_table_ 只记录当前驻留的 page_id → frame_id；它不是所有磁盘页面的目录。

/**
 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
 *
 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
 *
 * @param frame_id The frame ID / index of the frame we are creating a header for.
 */
/** @brief 构造 `FrameHeader`，把所有字段初始化为默认值。`frame_id` 是该帧的 ID／下标。 */
FrameHeader::FrameHeader(frame_id_t frame_id) : frame_id_(frame_id), data_(BUSTUB_PAGE_SIZE, 0) { Reset(); }

/**
 * @brief Get a raw const pointer to the frame's data.
 *
 * @return const char* A pointer to immutable data that the frame stores.
 */
/** @brief 取得指向帧数据的原始只读指针。 */
auto FrameHeader::GetData() const -> const char * { return data_.data(); }

/**
 * @brief Get a raw mutable pointer to the frame's data.
 *
 * @return char* A pointer to mutable data that the frame stores.
 */
/** @brief 取得指向帧数据的原始可写指针。 */
auto FrameHeader::GetDataMut() -> char * { return data_.data(); }

/**
 * @brief Resets a `FrameHeader`'s member fields.
 */
/** @brief 重置 `FrameHeader` 的成员字段。 */
void FrameHeader::Reset() {
  // 复用前清零页面字节和状态；调用方须保证无人使用该帧，并维护旧、新页表映射。
  std::fill(data_.begin(), data_.end(), 0);
  pin_count_.store(0);
  is_dirty_ = false;
}

/**
 * @brief Creates a new `BufferPoolManager` instance and initializes all fields.
 *
 * See the documentation for `BufferPoolManager` in "buffer/buffer_pool_manager.h" for more information.
 *
 * ### Implementation
 *
 * We have implemented the constructor for you in a way that makes sense with our reference solution. You are free to
 * change anything you would like here if it doesn't fit with you implementation.
 *
 * Be warned, though! If you stray too far away from our guidance, it will be much harder for us to help you. Our
 * recommendation would be to first implement the buffer pool manager using the stepping stones we have provided.
 *
 * Once you have a fully working solution (all Gradescope test cases pass), then you can try more interesting things!
 *
 * @param num_frames The size of the buffer pool.
 * @param disk_manager The disk manager.
 * @param log_manager The log manager. Please ignore this for P1.
 */
/**
 * @brief 创建 `BufferPoolManager` 并初始化全部字段。课程已按参考实现提供构造函数；若现有结构不适合你的
 * 实现，可以修改，但偏离框架过多会增加调试难度。建议先沿用给定结构通过全部测试，再尝试优化。
 * `num_frames` 是缓冲池大小，`disk_manager` 是磁盘管理器，`log_manager` 在 P1 中忽略。
 */
BufferPoolManager::BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager)
    : num_frames_(num_frames),
      next_page_id_(0),
      bpm_latch_(std::make_shared<std::mutex>()),
      replacer_(std::make_shared<ArcReplacer>(num_frames)),
      disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)),
      log_manager_(log_manager) {
  // Not strictly necessary...
  // 并非严格必要。
  std::scoped_lock latch(*bpm_latch_);

  // Initialize the monotonically increasing counter at 0.
  // 把单调递增计数器初始化为 0。
  next_page_id_.store(0);

  // Allocate all of the in-memory frames up front.
  // 预先分配所有内存帧。
  frames_.reserve(num_frames_);

  // The page table should have exactly `num_frames_` slots, corresponding to exactly `num_frames_` frames.
  // 页表预留 `num_frames_` 个槽位，与缓冲池中的帧数对应。
  page_table_.reserve(num_frames_);

  // Initialize all of the frame headers, and fill the free frame list with all possible frame IDs (since all frames are
  // initially free).
  // 初始化全部帧头；初始时所有帧均为空闲，因此把全部 Frame ID 放入空闲列表。
  for (size_t i = 0; i < num_frames_; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(static_cast<int>(i));
  }
}

/**
 * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
 */
/** @brief 销毁 `BufferPoolManager`，释放缓冲池使用的全部内存。 */
BufferPoolManager::~BufferPoolManager() = default;

/**
 * @brief Returns the number of frames that this buffer pool manages.
 */
/** @brief 返回该缓冲池管理的帧总数。 */
auto BufferPoolManager::Size() const -> size_t { return num_frames_; }

/**
 * @brief Allocates a new page on disk.
 *
 * ### Implementation
 *
 * You will maintain a thread-safe, monotonically increasing counter in the form of a `std::atomic<page_id_t>`.
 * See the documentation on [atomics](https://en.cppreference.com/w/cpp/atomic/atomic) for more information.
 *
 * TODO(P1): Add implementation.
 *
 * @return The page ID of the newly allocated page.
 */
/**
 * @brief 在磁盘上分配一个新页面。
 *
 * 使用线程安全、单调递增的 `std::atomic<page_id_t>` 计数器。
 *
 * TODO(P1)：补充实现。
 *
 * @return 新分配页面的 Page ID。
 */
// fetch_add 返回递增前的值；这里只分配页面身份，实际占用 frame 在后续访问时发生。
auto BufferPoolManager::NewPage() -> page_id_t { return next_page_id_.fetch_add(1); }

/**
 * @brief Removes a page from the database, both on disk and in memory.
 *
 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
 *
 * ### Implementation
 *
 * Think about all of the places that a page or a page's metadata could be, and use that to guide you on implementing
 * this function. You will probably want to implement this function _after_ you have implemented `CheckedReadPage` and
 * `CheckedWritePage`.
 *
 * You should call `DeallocatePage` in the disk scheduler to make the space available for new pages.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to delete.
 * @return `false` if the page exists but could not be deleted, `true` if the page didn't exist or deletion succeeded.
 */
/**
 * @brief 从数据库的磁盘和内存中删除一个页面。
 *
 * 若页面在缓冲池中且仍被 pin，则不做操作并返回 `false`；否则从
 * 内存和磁盘删除（若仍在缓冲池），并返回 `true`。应全面考虑页面及其元数据可能所在的位置，建议完成两个
 * `Checked*Page` 后再实现。调用磁盘调度器的 `DeallocatePage` 释放空间。
 *
 * TODO(P1)：补充实现。
 *
 * @param page_id 要删除的页面的 Page ID。
 * @return 页面存在但无法删除时返回 `false`；页面不存在或删除成功时返回 `true`。
 */
auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    // 不驻留不等于没有磁盘页：即使已被驱逐，删除请求仍须到达磁盘管理器。
    disk_scheduler_->DeallocatePage(page_id);
    return true;
  }
  if (frames_[iter->second]->pin_count_ != 0) {
    // 仍有使用者（也可能正在等待页面锁），不能删除或复用其 frame。
    return false;
  }
  std::shared_ptr<FrameHeader> ptr = frames_[iter->second];
  // 无 pin 后才能删除映射、归还 frame 并移除 ARC 活跃记录；删除无需回写旧脏数据。
  page_table_.erase(page_id);
  free_frames_.push_back(ptr->frame_id_);
  replacer_->Remove(ptr->frame_id_);
  disk_scheduler_->DeallocatePage(page_id);
  ptr->Reset();
  ptr->is_dirty_ = false;
  ptr->pin_count_ = 0;
  return true;
}

/**
 * @brief Acquires an optional write-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can only be 1 `WritePageGuard` reading/writing a page at a time. This allows data access to be both immutable
 * and mutable, meaning the thread that owns the `WritePageGuard` is allowed to manipulate the page's data however they
 * want. If a user wants to have multiple threads reading the page at the same time, they must acquire a `ReadPageGuard`
 * with `CheckedReadPage` instead.
 *
 * ### Implementation
 *
 * There are three main cases that you will have to implement. The first two are relatively simple: one is when there is
 * plenty of available memory, and the other is when we don't actually need to perform any additional I/O. Think about
 * what exactly these two cases entail.
 *
 * The third case is the trickiest, and it is when we do not have any _easily_ available memory at our disposal. The
 * buffer pool is tasked with finding memory that it can use to bring in a page of memory, using the replacement
 * algorithm you implemented previously to find candidate frames for eviction.
 *
 * Once the buffer pool has identified a frame for eviction, several I/O operations may be necessary to bring in the
 * page of data we want into the frame.
 *
 * There is likely going to be a lot of shared code with `CheckedReadPage`, so you may find creating helper functions
 * useful.
 *
 * These two functions are the crux of this project, so we won't give you more hints than this. Good luck!
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to write to.
 * @param access_type The type of page access.
 * @return std::optional<WritePageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`; otherwise, returns a `WritePageGuard` ensuring exclusive and mutable access to a page's data.
 */
/**
 * @brief 尝试取得页面的独占写锁 Guard；用户可按需指定 `AccessType`。
 *
 * 若无法把页面装入内存，返回 `nullopt`。
 * 页面数据只能通过 Page Guard 访问；同一页面同一时刻最多有一个 `WritePageGuard`，其拥有者可以读写数据。
 * 若要让多个线程同时只读，应使用 `CheckedReadPage`。
 *
 * ### 实现
 *
 * 实现主要有三种情况：存在充足空闲内存；目标页面已在内存、无需额外 I/O；以及没有容易取得的内存，需要
 * 用替换算法寻找可驱逐帧。选中驱逐帧后，可能需要若干磁盘 I/O 才能把目标页面装入。它与
 * `CheckedReadPage` 会共享大量逻辑，可考虑辅助函数。
 *
 * TODO(P1)：补充实现。
 *
 * @param page_id 要写入的页面的 Page ID。
 * @param access_type 页面访问类型。
 * @return 可选的锁 Guard；没有空闲帧时返回 `nullopt`，否则返回保证页面独占可写访问的 `WritePageGuard`。
 */

// 调用前持有 bpm_latch_，并保证传入 frame 在 I/O 完成前不会被复用。
auto BufferPoolManager::SingleRequest(bool is_write, frame_id_t frame_id, page_id_t page_id) -> std::future<bool> {
  std::promise<bool> promise = disk_scheduler_->CreatePromise();
  // 先取得等待端，再把完成端 promise 移入请求；不能返回已被移走的 promise。
  std::future<bool> future = promise.get_future();
  std::vector<DiskRequest> reqs;
  // 不使用 vector 的 initializer_list 构造：它要求从 const 元素拷贝，而 promise 不可拷贝。
  reqs.emplace_back(DiskRequest{is_write, frames_[frame_id]->GetDataMut(), page_id, std::move(promise)});
  disk_scheduler_->Schedule(reqs);
  return future;
}
// 调用前持有 bpm_latch_：查表、选择 frame、建立映射和 pin 必须作为整体协调。
auto BufferPoolManager::GetOrLoadPage(page_id_t page_id, AccessType access_type) -> std::optional<frame_id_t> {
  frame_id_t next = 0;
  if (auto iter = page_table_.find(page_id); iter != page_table_.end()) {
    // 命中已有驻留页，不需要磁盘读取。
    next = iter->second;
  } else if (!free_frames_.empty()) {
    // 使用预先分配的空闲 frame，而不是为每次缺页创建新的 FrameHeader。
    next = free_frames_.front();
    free_frames_.pop_front();
    SingleRequest(false, next, page_id).get();
  } else {
    if (std::optional<frame_id_t> op = replacer_->Evict(); op.has_value()) {
      next = op.value();
      std::shared_ptr<FrameHeader> ptr = frames_[next];
      // ARC 已淘汰旧记录，BPM 必须同步删除旧映射，否则以后查询旧 page 会出现假命中。
      page_table_.erase(ptr->page_id_);
      if (ptr->is_dirty_) {
        // 旧数据写完才能 Reset/读入新页：异步请求引用的就是这块 frame 内存。
        SingleRequest(true, next, ptr->page_id_).get();
      }
      ptr->Reset();
      SingleRequest(false, next, page_id).get();
    } else {
      // 所有 frame 都不可驱逐；Checked 接口把这个可预期的失败交给调用者处理。
      return std::nullopt;
    }
  }
  // 在释放 BPM 锁之前先 pin；随后等待页面锁期间也不能让别人驱逐此页。
  frames_[next]->pin_count_++;
  frames_[next]->page_id_ = page_id;
  page_table_[page_id] = next;
  replacer_->RecordAccess(next, page_id, access_type);
  replacer_->SetEvictable(next, false);
  // 调度器虽是异步的，本实现通过上面的 get() 等待加载完成才允许 Guard 使用数据。
  return next;
}
auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  std::optional<frame_id_t> op;
  {
    std::lock_guard<std::mutex> lock(*bpm_latch_);
    // std::cout << "7" << '\n';
    op = GetOrLoadPage(page_id, access_type);
  }
  // std::cout << "8" << '\n';
  // 已离开 BPM 锁作用域；下面构造 Guard 等待页面锁时，不会堵住所有其他 BPM 操作。
  if (!op.has_value()) {
    return std::nullopt;
  }
  // std::cout << "9" << '\n';

  return WritePageGuard(page_id, frames_[op.value()], replacer_, bpm_latch_, disk_scheduler_);
}

/**
 * @brief Acquires an optional read-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can be any number of `ReadPageGuard`s reading the same page of data at a time across different threads.
 * However, all data access must be immutable. If a user wants to mutate the page's data, they must acquire a
 * `WritePageGuard` with `CheckedWritePage` instead.
 *
 * ### Implementation
 *
 * See the implementation details of `CheckedWritePage`.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return std::optional<ReadPageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`; otherwise, returns a `ReadPageGuard` ensuring shared and read-only access to a page's data.
 */
/**
 * @brief 尝试取得页面的共享读锁 Guard；用户可按需指定 `AccessType`。
 *
 * 若无法把页面装入内存，返回 `nullopt`。
 * 同一页面可以被任意数量的 `ReadPageGuard` 跨线程同时读取，但所有访问必须只读；修改数据应通过
 * `CheckedWritePage` 取得 `WritePageGuard`。
 *
 * ### 实现
 *
 * 参见 `CheckedWritePage` 的实现细节。
 *
 * TODO(P1)：补充实现。
 *
 * @param page_id 要读取的页面的 Page ID。
 * @param access_type 页面访问类型。
 * @return 可选的锁 Guard；没有空闲帧时返回 `nullopt`，否则返回保证页面共享只读访问的 `ReadPageGuard`。
 */
auto BufferPoolManager::CheckedReadPage(page_id_t page_id, AccessType access_type) -> std::optional<ReadPageGuard> {
  // 阅读注意：当前读路径仍持有 BPM 锁构造 Guard，与写路径的缩小锁范围处理不对称。
  // 若页面锁持有者又请求 BPM 锁，可能形成环形等待；此处注释记录现状，不改变实现。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  std::optional<frame_id_t> op = GetOrLoadPage(page_id, access_type);
  if (!op.has_value()) {
    return std::nullopt;
  }
  return ReadPageGuard(page_id, frames_[op.value()], replacer_, bpm_latch_, disk_scheduler_);
}

/**
 * @brief A wrapper around `CheckedWritePage` that unwraps the inner value if it exists.
 *
 * If `CheckedWritePage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageWrite` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return WritePageGuard A page guard ensuring exclusive and mutable access to a page's data.
 */
/**
 * @brief 包装 `CheckedWritePage`，若其返回值存在则解包并返回；若返回 `nullopt`，本函数会终止整个进程。
 * 它只应用于测试和简化调用。只要缓冲池存在内存耗尽的可能，就应使用可处理失败的 Checked 接口。
 */
auto BufferPoolManager::WritePage(page_id_t page_id, AccessType access_type) -> WritePageGuard {
  // std::cout << "5" << '\n';
  auto guard_opt = CheckedWritePage(page_id, access_type);
  // std::cout << "6" << '\n';

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief A wrapper around `CheckedReadPage` that unwraps the inner value if it exists.
 *
 * If `CheckedReadPage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageRead` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return ReadPageGuard A page guard ensuring shared and read-only access to a page's data.
 */
/**
 * @brief 包装 `CheckedReadPage`，若其返回值存在则解包并返回；若返回 `nullopt`，本函数会终止整个进程。
 * 它只应用于测试和简化调用。只要缓冲池存在内存耗尽的可能，就应使用可处理失败的 Checked 接口。
 */
auto BufferPoolManager::ReadPage(page_id_t page_id, AccessType access_type) -> ReadPageGuard {
  auto guard_opt = CheckedReadPage(page_id, access_type);

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief Flushes a page's data out to disk unsafely.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * You should not take a lock on the page in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage` and
 * `CheckedWritePage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page to be flushed.
 * @return `false` if the page could not be found in the page table; otherwise, `true`.
 */
/**
 * @brief 以“不安全”方式把页面刷新到磁盘。页面被修改时才写盘；若页面不在内存则返回 `false`。本函数不应
 * 获取页面锁，因此必须谨慎决定何时切换 `is_dirty_`。建议完成 Checked 读写接口后再实现。页表中找不到
 * 页面返回 `false`，否则返回 `true`。TODO(P1)：补充实现。
 */
auto BufferPoolManager::FlushPageUnsafe(page_id_t page_id) -> bool {
  // BPM 锁只保护映射和 frame 身份，不阻止已存在的写 Guard 修改页面内容。
  // Unsafe 要求调用方另行保证写盘期间的数据稳定，不能把此锁当成页面锁。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return false;
  }
  std::shared_ptr<FrameHeader> ptr = frames_[iter->second];
  if (ptr->is_dirty_) {
    ptr->is_dirty_ = false;
    SingleRequest(true, page_table_[page_id], page_id).get();
  }
  return true;
}

/**
 * @brief Flushes a page's data out to disk safely.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * You should take a lock on the page in this function to ensure that a consistent state is flushed to disk.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `Flush` in the page guards, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page to be flushed.
 * @return `false` if the page could not be found in the page table; otherwise, `true`.
 */
/**
 * @brief 安全地把页面刷新到磁盘。页面被修改时才写盘；若页面不在内存则返回 `false`。应获取页面锁，确保
 * 写入磁盘的是一致状态。建议完成 Checked 读写接口和 Page Guard 的 `Flush` 后再实现。TODO(P1)：补充实现。
 */
auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  // 页面共享锁用于阻止并发写者，BPM 锁用于保护映射。
  // 阅读注意：这里同样存在持有 BPM 锁等待页面锁的次序问题，需结合 CheckedReadPage 阅读。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return false;
  }
  std::shared_ptr<FrameHeader> ptr = frames_[iter->second];
  std::shared_lock<std::shared_mutex> rwlock(ptr->rwlatch_);
  if (ptr->is_dirty_) {
    ptr->is_dirty_ = false;
    SingleRequest(true, page_table_[page_id], page_id).get();
  }
  return true;
}

/**
 * @brief Flushes all page data that is in memory to disk unsafely.
 *
 * You should not take locks on the pages in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 */
/**
 * @brief 以“不安全”方式把内存中的全部页面刷新到磁盘；不要获取页面锁，并谨慎处理 dirty 标志。建议完成
 * Checked 读写接口和 `FlushPage` 后再实现。TODO(P1)：补充实现。
 */
void BufferPoolManager::FlushAllPagesUnsafe() {
  // 遍历当前驻留页逐页等待写回；不加载已被驱逐的页，也不取得各页的内容锁。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  for (auto [page_id, frame_id] : page_table_) {
    std::shared_ptr<FrameHeader> ptr = frames_[frame_id];
    if (ptr->is_dirty_) {
      ptr->is_dirty_ = false;
      SingleRequest(true, page_table_[page_id], page_id).get();
    }
  }
}

/**
 * @brief Flushes all page data that is in memory to disk safely.
 *
 * You should take locks on the pages in this function to ensure that a consistent state is flushed to disk.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 */
/**
 * @brief 安全地把内存中的全部页面刷新到磁盘；应获取各页面的锁，保证写盘状态一致。建议完成 Checked
 * 读写接口和 `FlushPage` 后再实现。TODO(P1)：补充实现。
 */
void BufferPoolManager::FlushAllPages() {
  // 逐帧取得共享页面锁；与 FlushPage 一样，要注意 BPM 锁与页面锁的获取次序。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  for (auto [page_id, frame_id] : page_table_) {
    std::shared_ptr<FrameHeader> ptr = frames_[frame_id];
    std::shared_lock<std::shared_mutex> rwlock(ptr->rwlatch_);
    if (ptr->is_dirty_) {
      ptr->is_dirty_ = false;
      SingleRequest(true, page_table_[page_id], page_id).get();
    }
  }
}

/**
 * @brief Retrieves the pin count of a page. If the page does not exist in memory, return `std::nullopt`.
 *
 * This function is thread safe. Callers may invoke this function in a multi-threaded environment where multiple threads
 * access the same page.
 *
 * This function is intended for testing purposes. If this function is implemented incorrectly, it will definitely cause
 * problems with the test suite and autograder.
 *
 * # Implementation
 *
 * We will use this function to test if your buffer pool manager is managing pin counts correctly. Since the
 * `pin_count_` field in `FrameHeader` is an atomic type, you do not need to take the latch on the frame that holds the
 * page we want to look at. Instead, you can simply use an atomic `load` to safely load the value stored. You will still
 * need to take the buffer pool latch, however.
 *
 * Again, if you are unfamiliar with atomic types, see the official C++ docs
 * [here](https://en.cppreference.com/w/cpp/atomic/atomic).
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page we want to get the pin count of.
 * @return std::optional<size_t> The pin count if the page exists; otherwise, `std::nullopt`.
 */
/**
 * @brief 取得页面的 pin count；页面不在内存时返回 `nullopt`。本函数线程安全且主要供测试使用。
 * `FrameHeader::pin_count_` 是原子类型，读取它时无需获取该帧的页面锁，可以用原子 `load`；但仍须获取缓冲池
 * 全局锁来安全访问 page table 等元数据。TODO(P1)：补充实现。
 */
auto BufferPoolManager::GetPinCount(page_id_t page_id) -> std::optional<size_t> {
  // 原子计数只能保证计数值访问安全；仍需 BPM 锁保证查到的映射没有被删除或替换。
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (auto iter = page_table_.find(page_id); iter != page_table_.end()) {
    return frames_[iter->second]->pin_count_;
  }
  return std::nullopt;
}

}  // namespace bustub
