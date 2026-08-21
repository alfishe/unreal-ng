#pragma once

#ifndef MESSAGE_CENTER_MPMC_QUEUE_H
#define MESSAGE_CENTER_MPMC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <new>

// Bounded lock-free MPMC queue using a ring buffer with sequence numbers.
// Each slot has a sequence that indicates its state:
//   seq == pos        : slot is ready for writing
//   seq == pos + 1    : slot contains valid data, ready for reading
//   seq == pos + capacity : slot has been read, ready for next write cycle
//
// This design avoids ABA problems and provides wait-free progress for
// producers when the queue isn't full, and for consumers when it isn't empty.

template <typename T, size_t Capacity> class MPMCQueue {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  struct alignas(64) Slot {
    std::atomic<size_t> seq;
    T data;
  };

  alignas(64) Slot m_slots[Capacity];
  alignas(64) std::atomic<size_t> m_head{0}; // next position to dequeue
  alignas(64) std::atomic<size_t> m_tail{0}; // next position to enqueue

  static constexpr size_t MASK = Capacity - 1;

public:
  MPMCQueue() {
    for (size_t i = 0; i < Capacity; ++i) {
      m_slots[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  // Non-copyable, non-movable
  MPMCQueue(const MPMCQueue &) = delete;
  MPMCQueue &operator=(const MPMCQueue &) = delete;

  // Try to enqueue an item. Returns true on success, false if queue is full.
  bool try_push(const T &item) {
    size_t tail = m_tail.load(std::memory_order_relaxed);

    for (;;) {
      Slot &slot = m_slots[tail & MASK];
      size_t seq = slot.seq.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

      if (diff == 0) {
        // Slot is ready for writing, try to claim it
        if (m_tail.compare_exchange_weak(tail, tail + 1,
                                         std::memory_order_relaxed)) {
          slot.data = item;
          slot.seq.store(tail + 1, std::memory_order_release);
          return true;
        }
        // CAS failed, tail was updated, retry with new value
      } else if (diff < 0) {
        // Queue is full
        return false;
      } else {
        // Another thread claimed this slot, reload tail
        tail = m_tail.load(std::memory_order_relaxed);
      }
    }
  }

  // Try to dequeue an item. Returns true on success, false if queue is empty.
  bool try_pop(T &item) {
    size_t head = m_head.load(std::memory_order_relaxed);

    for (;;) {
      Slot &slot = m_slots[head & MASK];
      size_t seq = slot.seq.load(std::memory_order_acquire);
      intptr_t diff =
          static_cast<intptr_t>(seq) - static_cast<intptr_t>(head + 1);

      if (diff == 0) {
        // Slot has data, try to claim it
        if (m_head.compare_exchange_weak(head, head + 1,
                                         std::memory_order_relaxed)) {
          item = slot.data;
          slot.seq.store(head + Capacity, std::memory_order_release);
          return true;
        }
        // CAS failed, head was updated, retry with new value
      } else if (diff < 0) {
        // Queue is empty
        return false;
      } else {
        // Another thread took this slot, reload head
        head = m_head.load(std::memory_order_relaxed);
      }
    }
  }

  // Approximate size (may be stale)
  size_t size_approx() const {
    size_t tail = m_tail.load(std::memory_order_relaxed);
    size_t head = m_head.load(std::memory_order_relaxed);
    return tail >= head ? tail - head : 0;
  }

  bool empty() const { return size_approx() == 0; }

  static constexpr size_t capacity() { return Capacity; }
};

#endif // MESSAGE_CENTER_MPMC_QUEUE_H
