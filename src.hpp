#ifndef SRC_HPP
#define SRC_HPP
#include <cstddef>

/**
 * 枚举类，用于枚举可能的置换策略
 */
enum class ReplacementPolicy { kDEFAULT = 0, kFIFO, kLRU, kMRU, kLRU_K };

/**
 * @brief 该类用于维护每一个页对应的信息以及其访问历史，用于在尝试置换时查询需要的信息。
 */
class PageNode {
public:
  PageNode(std::size_t page_id, std::size_t timestamp, std::size_t max_history) 
    : page_id_(page_id), max_history_(max_history), history_count_(0) {
    history_ = new std::size_t[max_history_];
    history_[history_count_++] = timestamp;
  }

  ~PageNode() {
    delete[] history_;
  }

  std::size_t GetPageId() const {
    return page_id_;
  }

  void AddAccess(std::size_t timestamp) {
    if (history_count_ < max_history_) {
      history_[history_count_++] = timestamp;
    } else {
      // Shift all elements left and add new timestamp at the end
      for (std::size_t i = 0; i < max_history_ - 1; ++i) {
        history_[i] = history_[i + 1];
      }
      history_[max_history_ - 1] = timestamp;
    }
  }

  std::size_t GetFirstAccessTime() const {
    return history_[0];
  }

  std::size_t GetLastAccessTime() const {
    return history_[history_count_ - 1];
  }

  std::size_t GetKthLastAccessTime(std::size_t k) const {
    if (history_count_ < k) {
      return 0; // Return 0 to represent -infinity
    }
    return history_[history_count_ - k];
  }

  std::size_t GetAccessCount() const {
    return history_count_;
  }

private:
  std::size_t page_id_;
  std::size_t max_history_;
  std::size_t history_count_;
  std::size_t* history_;
};

class ReplacementManager {
public:
  constexpr static std::size_t npos = -1;

  ReplacementManager() = delete;

  /**
   * @brief 初始化整个类
   * @param max_size 缓存池可以容纳的页数量的上限
   * @param k LRU-K所基于的常数k，在类销毁前不会变更
   * @param default_policy 在置换时，如果没有显式指示，则默认使用default_policy作为策略
   * @note 我们将保证default_policy的值不是ReplacementPolicy::kDEFAULT。
   */
  ReplacementManager(std::size_t max_size, std::size_t k, ReplacementPolicy default_policy) 
    : max_size_(max_size), k_(k), default_policy_(default_policy), current_size_(0), timestamp_(0) {
    pages_ = new PageNode*[max_size_];
    for (std::size_t i = 0; i < max_size_; ++i) {
      pages_[i] = nullptr;
    }
  }

  /**
   * @brief 析构函数
   * @note 我们将对代码进行Valgrind Memcheck，请保证你的代码不发生内存泄漏
   */
  ~ReplacementManager() {
    for (std::size_t i = 0; i < max_size_; ++i) {
      if (pages_[i] != nullptr) {
        delete pages_[i];
      }
    }
    delete[] pages_;
  }

  /**
   * @brief 重设当前默认的缓存置换政策
   * @param default_policy 新的默认政策，保证default_policy不是ReplacementPolicy::kDEFAULT
   */
  void SwitchDefaultPolicy(ReplacementPolicy default_policy) {
    default_policy_ = default_policy;
  }

  /**
   * @brief 访问某个页面。
   * @param page_id 访问页的编号
   * @param evict_id 需要被置换的页编号，如果不需要置换请将其设置为npos
   * @param policy 如果需要置换，那么置换所基于的策略
   * (a) 若访问的页已经在缓存池中，那么直接记录其访问信息。
   * (b) 若访问的页不在缓存池中，那么：
   *    1. 若缓存池已满，就从中依照policy置换一个页（彻底删除其对应节点），并将新访问的页加入缓存池，记录其访问
   *    2. 若缓存池未满，则直接将其加入缓存池并记录其访问
   * @note 我们不保证page_id在调用间连续，也不保证page_id的范围，只保证page_id在std::size_t内
   */
  void Visit(std::size_t page_id, std::size_t &evict_id, ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) {
    if (policy == ReplacementPolicy::kDEFAULT) {
      policy = default_policy_;
    }

    ++timestamp_;
    evict_id = npos;

    // Check if page already exists
    for (std::size_t i = 0; i < max_size_; ++i) {
      if (pages_[i] != nullptr && pages_[i]->GetPageId() == page_id) {
        pages_[i]->AddAccess(timestamp_);
        return;
      }
    }

    // Page not in cache
    if (current_size_ < max_size_) {
      // Cache not full, add new page
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] == nullptr) {
          pages_[i] = new PageNode(page_id, timestamp_, k_ + 1);
          ++current_size_;
          return;
        }
      }
    } else {
      // Cache full, need to evict
      evict_id = FindEvictPage(policy);
      RemovePage(evict_id);
      
      // Add new page
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] == nullptr) {
          pages_[i] = new PageNode(page_id, timestamp_, k_ + 1);
          ++current_size_;
          return;
        }
      }
    }
  }

  /**
   * @brief 强制地删除特定的页（无论缓存池是否已满）
   * @param page_id 被删除页的编号
   * @return 如果成功删除，则返回true; 如果该页不存在于缓存池中，则返回false
   * 如果page_id存在于缓存池中，则删除它；否则，直接返回false
   */
  bool RemovePage(std::size_t page_id) {
    for (std::size_t i = 0; i < max_size_; ++i) {
      if (pages_[i] != nullptr && pages_[i]->GetPageId() == page_id) {
        delete pages_[i];
        pages_[i] = nullptr;
        --current_size_;
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 查询特定策略下首先被置换的页
   * @param policy 置换策略
   * @return 当前策略下会被置换的页的编号。若缓存池没满，则返回npos
   * 不对缓存池做任何修改，只查询在需要置换的情况下，基于给定的政策，应该置换哪个页。
   * @note 如果缓存池没有满，请直接返回npos
   */
  [[nodiscard]] std::size_t TryEvict(ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) const {
    if (current_size_ < max_size_) {
      return npos;
    }

    if (policy == ReplacementPolicy::kDEFAULT) {
      policy = default_policy_;
    }

    return FindEvictPage(policy);
  }

  /**
   * @brief 返回当前缓存管理器是否为空。
   */
  [[nodiscard]] bool Empty() const {
    return current_size_ == 0;
  }

  /**
   * @brief 返回当前缓存管理器是否已满（即是否页数量已经达到上限）
   */
  [[nodiscard]] bool Full() const {
    return current_size_ == max_size_;
  }

  /**
   * @brief 返回当前缓存管理器中页的数量
   */
  [[nodiscard]] std::size_t Size() const {
    return current_size_;
  }

private:
  std::size_t max_size_;
  std::size_t k_;
  ReplacementPolicy default_policy_;
  std::size_t current_size_;
  std::size_t timestamp_;
  PageNode** pages_;

  std::size_t FindEvictPage(ReplacementPolicy policy) const {
    std::size_t evict_idx = npos;
    
    if (policy == ReplacementPolicy::kFIFO) {
      // Find page with earliest first access time
      std::size_t min_time = npos;
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] != nullptr) {
          std::size_t first_time = pages_[i]->GetFirstAccessTime();
          if (evict_idx == npos || first_time < min_time) {
            min_time = first_time;
            evict_idx = i;
          }
        }
      }
    } else if (policy == ReplacementPolicy::kLRU) {
      // Find page with earliest last access time
      std::size_t min_time = npos;
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] != nullptr) {
          std::size_t last_time = pages_[i]->GetLastAccessTime();
          if (evict_idx == npos || last_time < min_time) {
            min_time = last_time;
            evict_idx = i;
          }
        }
      }
    } else if (policy == ReplacementPolicy::kMRU) {
      // Find page with latest last access time
      std::size_t max_time = 0;
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] != nullptr) {
          std::size_t last_time = pages_[i]->GetLastAccessTime();
          if (evict_idx == npos || last_time > max_time) {
            max_time = last_time;
            evict_idx = i;
          }
        }
      }
    } else if (policy == ReplacementPolicy::kLRU_K) {
      // Find page with earliest k-th last access time
      // If access count < k, treat k-th time as 0 (priority eviction)
      // Among those with count < k, evict the one with earliest first access
      std::size_t evict_kth_time = npos;
      std::size_t evict_first_time = npos;
      bool evict_has_less_than_k = false;
      
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] != nullptr) {
          std::size_t access_count = pages_[i]->GetAccessCount();
          std::size_t kth_time = pages_[i]->GetKthLastAccessTime(k_);
          std::size_t first_time = pages_[i]->GetFirstAccessTime();
          bool has_less_than_k = (access_count < k_);
          
          if (evict_idx == npos) {
            evict_idx = i;
            evict_kth_time = kth_time;
            evict_first_time = first_time;
            evict_has_less_than_k = has_less_than_k;
          } else {
            // Compare based on LRU-K rules
            bool should_replace = false;
            
            if (evict_has_less_than_k && has_less_than_k) {
              // Both have less than k accesses, compare first access time
              should_replace = (first_time < evict_first_time);
            } else if (has_less_than_k && !evict_has_less_than_k) {
              // Current has less than k, should be evicted
              should_replace = true;
            } else if (!has_less_than_k && evict_has_less_than_k) {
              // Current has >= k, evict has less, don't replace
              should_replace = false;
            } else {
              // Both have >= k accesses, compare k-th access time
              should_replace = (kth_time < evict_kth_time);
            }
            
            if (should_replace) {
              evict_idx = i;
              evict_kth_time = kth_time;
              evict_first_time = first_time;
              evict_has_less_than_k = has_less_than_k;
            }
          }
        }
      }
    }

    if (evict_idx != npos && pages_[evict_idx] != nullptr) {
      return pages_[evict_idx]->GetPageId();
    }
    return npos;
  }
};
#endif
