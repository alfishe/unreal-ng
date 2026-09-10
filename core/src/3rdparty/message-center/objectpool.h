#pragma once

#ifndef MESSAGE_CENTER_OBJECTPOOL_H
#define MESSAGE_CENTER_OBJECTPOOL_H

#include <memory>
#include <mutex>
#include <vector>

/// region <Object Pool>

/// Thread-safe object pool for reducing heap allocations
/// Objects are recycled instead of being deleted and reallocated
///
/// Usage:
///   ObjectPool<MyClass> pool;
///   MyClass* obj = pool.acquire();
///   // ... use object ...
///   pool.release(obj);
///
template <typename T> class ObjectPool {
private:
  std::vector<T *> m_pool;
  mutable std::mutex m_mutex; // mutable allows locking in const methods
  size_t m_maxSize;
  size_t m_allocated;

public:
  /// Constructor
  /// @param initialSize - Number of objects to pre-allocate
  /// @param maxSize - Maximum pool size (0 = unlimited)
  explicit ObjectPool(size_t initialSize = 0, size_t maxSize = 1024)
      : m_maxSize(maxSize), m_allocated(0) {
    m_pool.reserve(initialSize);

    // Pre-allocate objects
    for (size_t i = 0; i < initialSize; ++i) {
      m_pool.push_back(new T());
      m_allocated++;
    }
  }

  /// Destructor - cleans up all pooled objects
  ~ObjectPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (T *obj : m_pool) {
      delete obj;
    }
    m_pool.clear();
  }

  // Disable copy and assignment
  ObjectPool(const ObjectPool &) = delete;
  ObjectPool &operator=(const ObjectPool &) = delete;

  /// Acquire an object from the pool
  /// If pool is empty, allocates a new object
  /// @return Pointer to object (never null)
  T *acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_pool.empty()) {
      // Reuse object from pool
      T *obj = m_pool.back();
      m_pool.pop_back();
      return obj;
    }

    // Pool is empty - allocate new object
    m_allocated++;
    return new T();
  }

  /// Return an object to the pool for reuse
  /// If pool is at max size, the object is deleted instead
  /// @param obj - Object to return (must not be null)
  void release(T *obj) {
    if (obj == nullptr)
      return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if pool has room
    if (m_maxSize == 0 || m_pool.size() < m_maxSize) {
      // Return to pool for reuse
      m_pool.push_back(obj);
    } else {
      // Pool is full - delete the object
      delete obj;
      m_allocated--;
    }
  }

  /// Get current pool size (available objects)
  size_t size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pool.size();
  }

  /// Get total allocated objects (in pool + in use)
  size_t allocated() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocated;
  }

  /// Clear the pool and delete all objects
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (T *obj : m_pool) {
      delete obj;
    }
    m_pool.clear();
    m_allocated = 0;
  }
};

/// endregion </Object Pool>

#endif // MESSAGE_CENTER_OBJECTPOOL_H
