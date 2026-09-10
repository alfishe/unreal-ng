#pragma once

#ifndef MESSAGE_CENTER_OBJECTPOOL_LOCKFREE_H
#define MESSAGE_CENTER_OBJECTPOOL_LOCKFREE_H

#include <atomic>
#include <cstddef>

// Lock-free object pool using a Treiber stack (lock-free LIFO).
// Objects embed a next pointer for the free list, avoiding separate nodes.
// For Message objects, we repurpose the 'obj' pointer when in the pool.

template <typename T> class ObjectPoolLockFree {
  struct Node {
    T data;
    Node *next;
  };

  std::atomic<Node *> m_freeList{nullptr};
  std::atomic<size_t> m_allocated{0};
  size_t m_maxSize;

public:
  explicit ObjectPoolLockFree(size_t initialSize = 0, size_t maxSize = 4096)
      : m_maxSize(maxSize) {
    for (size_t i = 0; i < initialSize; ++i) {
      Node *node = new Node();
      pushFree(node);
      m_allocated.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ~ObjectPoolLockFree() {
    Node *node = m_freeList.load(std::memory_order_relaxed);
    while (node) {
      Node *next = node->next;
      delete node;
      node = next;
    }
  }

  ObjectPoolLockFree(const ObjectPoolLockFree &) = delete;
  ObjectPoolLockFree &operator=(const ObjectPoolLockFree &) = delete;

  T *acquire() {
    Node *node = popFree();
    if (node) {
      return &node->data;
    }
    // Pool empty, allocate new
    node = new Node();
    m_allocated.fetch_add(1, std::memory_order_relaxed);
    return &node->data;
  }

  void release(T *obj) {
    if (!obj)
      return;
    // Get Node* from T* (T is at offset 0 in Node)
    Node *node = reinterpret_cast<Node *>(obj);

    size_t allocated = m_allocated.load(std::memory_order_relaxed);
    if (m_maxSize > 0 && allocated > m_maxSize) {
      delete node;
      m_allocated.fetch_sub(1, std::memory_order_relaxed);
    } else {
      pushFree(node);
    }
  }

  size_t allocated() const {
    return m_allocated.load(std::memory_order_relaxed);
  }

private:
  void pushFree(Node *node) {
    Node *oldHead = m_freeList.load(std::memory_order_relaxed);
    do {
      node->next = oldHead;
    } while (!m_freeList.compare_exchange_weak(oldHead, node,
                                                std::memory_order_release,
                                                std::memory_order_relaxed));
  }

  Node *popFree() {
    Node *oldHead = m_freeList.load(std::memory_order_acquire);
    while (oldHead) {
      Node *newHead = oldHead->next;
      if (m_freeList.compare_exchange_weak(oldHead, newHead,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
        return oldHead;
      }
    }
    return nullptr;
  }
};

#endif // MESSAGE_CENTER_OBJECTPOOL_LOCKFREE_H
