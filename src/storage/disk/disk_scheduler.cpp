//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_scheduler.h"
#include <optional>
#include <utility>
#include <vector>
#include "common/macros.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

// 请求生命周期：调用者取得 future → Schedule 移交请求 → 后台执行 I/O → promise 通知完成。
// data_ 是借用的页面内存；入队不复制页面内容，调用者必须保证 I/O 完成前内存仍有效。

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // 创建唯一的后台工作线程；队列为空时，Get() 会阻塞等待新请求。
  background_thread_.emplace([&] { StartWorkerThread(); });
}

DiskScheduler::~DiskScheduler() {
  // 用空 optional 作为结束标记；正常销毁时，先处理排在标记前的请求，再退出。
  request_queue_.Put(std::nullopt);
  if (background_thread_.has_value()) {
    // 等待线程结束，避免队列和 scheduler 成员已析构，线程却还在访问它们。
    background_thread_->join();
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Schedules a request for the DiskManager to execute.
 *
 * @param requests The requests to be scheduled.
 */
void DiskScheduler::Schedule(std::vector<DiskRequest> &requests) {
  // DiskRequest 含不可拷贝的 promise，只能移动入队；调用后不能再依赖原请求的 promise。
  for (auto &req : requests) {
    // request_queue_.Put(std::optional<DiskRequest>{req});
    // request_queue_.Put(req);
    request_queue_.Put(std::move(req));
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 */
void DiskScheduler::StartWorkerThread() {
  // 每次 Get() 消费一个请求；读写的是整个数据库页面，由 DiskManager 负责文件访问。
  for (auto req = request_queue_.Get(); req != std::nullopt; req = request_queue_.Get()) {
    if (req->is_write_) {
      disk_manager_->WritePage(req->page_id_, req->data_);
    } else {
      disk_manager_->ReadPage(req->page_id_, req->data_);
    }
    // 先完成数据读写，再唤醒等待 future 的调用者。
    req->callback_.set_value(true);
  }
}

}  // namespace bustub
