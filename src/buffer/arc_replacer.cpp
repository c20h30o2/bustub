// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::EvictFromMru() -> std::optional<frame_id_t> {
  for (auto iter = mru_.rbegin(); iter != mru_.rend(); ++iter) {
    if (alive_map_[*iter]->evictable_) {
      // std::cout << "curr_size:" << curr_size_ << " evict:" << *iter << '\n';
      --curr_size_;
      auto ptr = alive_map_[*iter];
      mru_ghost_.emplace_front(ptr->page_id_);
      ptr->arc_status_ = ArcStatus::MRU_GHOST;
      ptr->ghost_iter_ = mru_ghost_.begin();
      ptr->evictable_ = true;
      ghost_map_[ptr->page_id_] = ptr;
      alive_map_.erase(*iter);
      mru_.erase(--iter.base());
      return ptr->frame_id_;
    }
  }
  return std::nullopt;
}
auto ArcReplacer::EvictFromMfu() -> std::optional<frame_id_t> {
  for (auto iter = mfu_.rbegin(); iter != mfu_.rend(); ++iter) {
    if (alive_map_[*iter]->evictable_) {
      // std::cout << curr_size_ << " evict:" << *iter << '\n';
      --curr_size_;
      auto ptr = alive_map_[*iter];
      mfu_ghost_.emplace_front(ptr->page_id_);
      ptr->ghost_iter_ = mfu_ghost_.begin();
      ptr->arc_status_ = ArcStatus::MFU_GHOST;
      ptr->evictable_ = true;
      ghost_map_[ptr->page_id_] = ptr;
      alive_map_.erase(*iter);
      mfu_.erase(--iter.base());
      return ptr->frame_id_;
    }
  }
  return std::nullopt;
}
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);
  if (mru_.size() >= mru_target_size_) {
    if (auto op = EvictFromMru(); op.has_value()) {
      return op;
    }
    return EvictFromMfu();
  }
  if (auto op = EvictFromMfu(); op.has_value()) {
    return op;
  }
  return EvictFromMru();
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);
  if (auto iter = alive_map_.find(frame_id); iter != alive_map_.end() && iter->second->page_id_ == page_id) {
    if (iter->second->arc_status_ == ArcStatus::MRU) {
      // std::cout << "record hit mru, frame:" << frame_id << " page:" << page_id << " curr_size:" << curr_size_ <<
      // '\n';
      mfu_.splice(mfu_.begin(), mru_, iter->second->alive_iter_);
    } else {
      // std::cout << "record hit mfu, frame:" << frame_id << " page:" << page_id << " curr_size:" << curr_size_ <<
      // '\n';
      mfu_.splice(mfu_.begin(), mfu_, iter->second->alive_iter_);
    }
    iter->second->alive_iter_ = mfu_.begin();
    iter->second->arc_status_ = ArcStatus::MFU;
    // } else if (auto iter = ghost_map_.find(page_id); iter != ghost_map_.end() && iter->second->page_id_==frame_id)
    // {
  } else if (auto iter = ghost_map_.find(page_id); iter != ghost_map_.end()) {
    auto ptr = iter->second;
    ptr->frame_id_ = frame_id;
    mfu_.emplace_front(ptr->frame_id_);
    if (iter->second->arc_status_ == ArcStatus::MRU_GHOST) {
      // std::cout << "record hit mru_ghost, frame:" << frame_id << " page:" << page_id << " curr_size:" << curr_size_
      //           << '\n';
      if (mru_target_size_ < replacer_size_) {
        ++mru_target_size_;
      }
      mru_ghost_.erase(ptr->ghost_iter_);
    } else {
      // std::cout << "record hit mfu_ghost_, frame:" << frame_id << " page:" << page_id << " curr_size:" << curr_size_
      //           << '\n';
      if (mru_target_size_ > 0) {
        --mru_target_size_;
      }
      mfu_ghost_.erase(ptr->ghost_iter_);
    }
    ptr->alive_iter_ = mfu_.begin();
    ptr->arc_status_ = ArcStatus::MFU;
    ptr->evictable_ = false;
    alive_map_[ptr->frame_id_] = ptr;
    ghost_map_.erase(iter);
  } else {
    // std::cout << "record hit empty, frame:" << frame_id << " page:" << page_id << " curr_size:" << curr_size_ <<
    // '\n';
    auto ptr = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
    if (mru_.size() + mru_ghost_.size() >= replacer_size_) {
      // std::cout << "hit empty,mru_ghost pop:" << mru_ghost_.back() << '\n';
      ghost_map_.erase(mru_ghost_.back());
      mru_ghost_.pop_back();
    } else if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() >= 2 * replacer_size_) {
      // std::cout << "hit empty,mfu_ghost pop:" << mfu_ghost_.back() << '\n';
      ghost_map_.erase(mfu_ghost_.back());
      mfu_ghost_.pop_back();
    }
    mru_.emplace_front(ptr->frame_id_);
    ptr->alive_iter_ = mru_.begin();
    alive_map_[ptr->frame_id_] = ptr;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  // std::cout << "set_evictable:frame_id:" << frame_id << " able:" << set_evictable << '\n';
  if (auto iter = alive_map_.find(frame_id); iter != alive_map_.end()) {
    if (set_evictable == false && iter->second->evictable_ == true) {
      // std::cout << curr_size_ << "set" << '\n';
      --curr_size_;
    }
    if (set_evictable == true && iter->second->evictable_ == false) {
      ++curr_size_;
    }
    iter->second->evictable_ = set_evictable;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  auto iter = alive_map_.find(frame_id);
  if (iter == alive_map_.end()) {
    return;
  }
  assert(iter->second->evictable_);
  --curr_size_;
  switch (iter->second->arc_status_) {
    case bustub::ArcStatus::MRU: {
      mru_.erase(iter->second->alive_iter_);
      // std::cout << "remove in mru, frame:" << frame_id << " page:" << iter->second->page_id_
      //           << " curr_size:" << curr_size_ << '\n';
      break;
    }
    case bustub::ArcStatus::MFU: {
      mfu_.erase(iter->second->alive_iter_);
      // std::cout << "remove in mfu, frame:" << frame_id << " page:" << iter->second->page_id_
      //           << " curr_size:" << curr_size_ << '\n';
      break;
    }
    case bustub::ArcStatus::MRU_GHOST: {
    }
    case bustub::ArcStatus::MFU_GHOST: {
    }
  }
  alive_map_.erase(iter);
}
/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}
}  // namespace bustub
