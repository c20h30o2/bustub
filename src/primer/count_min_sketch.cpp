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
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fmt/chrono.h"

namespace bustub {

// 复习顺序：构造函数 → Insert → Count → Merge/Clear → TopK → 移动构造/赋值。
// sketch_ 用一维数组表示 depth 行、width 列；每行使用不同的哈希函数。
// 每个计数器是原子的，但整次 Insert、Count、Merge、Clear 并不是原子快照操作。

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
  if (depth_ == 0 || width_ == 0) {
    throw std::invalid_argument("width|depth is zero");
  }
  // 一次分配固定大小的数组，避免容器扩容时要求复制或移动 std::atomic 元素。
  sketch_ = std::make_unique<std::atomic<uint32_t>[]>(width_ * depth_);
  for (size_t i{}; i < width_ * depth_; ++i) {
    sketch_[i].store(0);
  }

  /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
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
    : width_(std::exchange(other.width_, 0)), depth_(std::exchange(other.depth_, 0)) {
  sketch_ = std::move(other.sketch_);
  // hash_functions_ = std::move(other.hash_functions_);
  // HashFunction 返回的 lambda 捕获 this。直接移动旧函数会继续引用 other，
  // 而 other.width_ 已被 exchange 置零；因此在新对象上重新生成函数。
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; ++i) {
    hash_functions_.emplace_back(HashFunction(i));
  }
  /** @TODO(student) Implement this function! */
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  // 阅读注意：当前赋值只接管尺寸和计数器，保留目标原有的哈希函数。
  // 与移动构造对照时，应检查深度变化、自移动等情况；这里并未覆盖这些边界。
  /** @TODO(student) Implement this function! */
  width_ = std::exchange(other.width_, 0);
  depth_ = std::exchange(other.depth_, 0);
  sketch_ = std::move(other.sketch_);
  // hash_functions_ = std::move(other.hash_functions_);
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  // 每行只增加一个桶。行 i、列 j 对应一维下标 i * width_ + j。
  /** @TODO(student) Implement this function! */
  for (size_t i{}; i < depth_; ++i) {
    sketch_[i * width_ + hash_functions_[i](item) % width_].fetch_add(1);
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  // 相同尺寸、相同种子规则才能逐桶相加；结果相当于汇总两份插入记录。
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  /** @TODO(student) Implement this function! */
  for (size_t i{}; i < width_ * depth_; ++i) {
    sketch_[i].fetch_add(other.sketch_[i].load());
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  // 哈希碰撞会把别人的次数算进来；在计数器不溢出、无并发清零时，
  // 各行估计都不会低于真实次数，取最小值可减轻碰撞造成的高估。
  uint32_t ans = UINT32_MAX;
  for (size_t i{}; i < depth_; ++i) {
    // %0 err
    ans = std::min(ans, sketch_[hash_functions_[i](item) % width_ + i * width_].load());
  }
  return ans;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  // 只清零计数，不改变数组尺寸和哈希规则。
  /** @TODO(student) Implement this function! */
  for (size_t i{}; i < width_ * depth_; ++i) {
    sketch_[i].store(0);
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  // sketch 不保存原始 key，所以需要调用者提供候选集合。
  // 当前实现把全部候选放入大顶堆，按估计次数从大到小取出 k 个。
  // 阅读注意：下方未限制 k，要求 k <= candidates.size()，否则会从空堆取值。
  /** @TODO(student) Implement this function! */
  auto comp = [](const std::pair<KeyType, uint32_t> &i, const std::pair<KeyType, uint32_t> &j) -> bool {
    return i.second < j.second;
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
