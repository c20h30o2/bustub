//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
  /** @TODO(student) Implement this function! */

  /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
  sketch_.reserve(depth_);
  // error
  // for (auto &i : sketch_) {
  //   i.reserve(width_);
  //   for (auto &j : i) {
  //     j = std::move(std::atomic<uint32_t>(0u));
  //   }
  // }

  // error
  // for(size_t i{};i<depth_;++i){
  //     sketch_[i].emplace_back(width_,0u);
  // }

  // error
  // for (size_t i{}; i < depth_; ++i) {
  //   sketch_.emplace_back(width, 0u);
  // }

  // error
  // for (size_t i{}; i < depth_; ++i) {
  //   sketch_[i].reserve(width_);
  //   for (size_t j{}; j < width_; ++j) {
  //     sketch_[i].emplace_back(0u);
  //   }
  // }

}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept
    : width_(std::exchange(other.width_, 0)),
      depth_(std::exchange(other.depth_, 0)),
      sketch_(std::move(other.sketch_)) {
  /** @TODO(student) Implement this function! */
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  /** @TODO(student) Implement this function! */
  width_ = std::exchange(other.width_, 0);
  depth_ = std::exchange(other.depth_, 0);
  sketch_ = std::move(other.sketch_);
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  /** @TODO(student) Implement this function! */
  for (size_t i{}; i < depth_; ++i) {
    sketch_[i][hash_functions_[i](item) % width_].fetch_add(1);
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  /** @TODO(student) Implement this function! */
  std::lock_guard lock(mutex_);
  for (size_t i{}; i < depth_; ++i) {
    for (size_t j{}; j < width_; ++j) {
      sketch_[i][j].fetch_add(other.sketch_[i][j].load());
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t ans = 0;
  for (size_t i{}; i < depth_; ++i) {
    ans = std::min(ans, sketch_[depth_][hash_functions_[depth_](item) % width_].load());
  }
  return ans;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  /** @TODO(student) Implement this function! */
  std::lock_guard lock(mutex_);
  width_ = 0;
  depth_ = 0;
  sketch_.clear();
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  /** @TODO(student) Implement this function! */
  auto comp = [](const std::pair<KeyType, uint32_t> &i, const std::pair<KeyType, uint32_t> &j) -> bool {
    return i.second > j.second;
  };
  std::priority_queue<std::pair<KeyType, uint32_t>, std::vector<std::pair<KeyType, uint32_t>>, decltype(comp)> que(
      comp);
  for (size_t i{}; i < candidates.size(); ++i) {
    que.push({candidates[i], Count(candidates[i])});
  }
  std::vector<std::pair<KeyType, uint32_t>> ans(k);
  for (std::pair<KeyType, uint32_t> &pa : ans) {
    pa = que.top();
    que.pop();
  }
  return ans;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
