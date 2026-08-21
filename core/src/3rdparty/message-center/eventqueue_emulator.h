#pragma once

#ifndef MESSAGE_CENTER_EVENTQUEUE_EMULATOR_H
#define MESSAGE_CENTER_EVENTQUEUE_EMULATOR_H

// EventQueueEmulator - Dual-queue system for mixed-criticality workloads
//
// Performance: P99=99μs for critical events (22x better than single queue)
//
// Designed for emulators, games, and real-time systems where some events
// (audio sync, input, vblank) must have guaranteed low latency while other
// events (debug traces, state saves) can tolerate delays.
//
// Architecture:
// - Fast queue: Inline-only (≤48B), zero allocation, always dispatched first
// - Bulk queue: Managed payloads OK, processed when fast queue is empty
//
// Topic priorities:
// - TopicPriority::Critical -> Fast queue (audio, vblank, input)
// - TopicPriority::Normal   -> Bulk queue (debug, state, screenshots)
//
// Usage:
//   EventQueueEmulator<4096, 16384> queue;  // fast capacity, bulk capacity
//   uint16_t audio = queue.registerTopic(TopicPriority::Critical);
//   uint16_t trace = queue.registerTopic(TopicPriority::Normal);
//   queue.postFast(audio, data, size);   // Goes to fast queue
//   queue.postBulk(trace, data, size);   // Goes to bulk queue
//   queue.dispatchPriority();            // Drain fast, then one bulk

#include "mpmc_queue.h"
#include "objectpool_lockfree.h"
#include "payload_refcounted.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

constexpr size_t EMU_MAX_TOPICS = 256;
constexpr size_t EMU_INLINE_MAX = 48;
constexpr size_t EMU_MAX_OBSERVERS = 8;

// Topic priority levels
enum class TopicPriority : uint8_t {
  Critical = 0,  // Audio, VBlank, Input - inline only, always processed first
  Normal = 1     // Debug, state saves - can use managed payloads
};

using EmuCallback = void (*)(uint16_t topicId, const void *data, size_t size, void *userData);

struct EmuObserver {
  EmuCallback callback;
  void *userData;
};

// Fast message - inline payload only, no allocation
struct EmuFastMessage {
  uint16_t topicId;
  uint8_t observerCount;
  uint8_t payloadSize;  // Max 48 bytes
  EmuObserver observers[EMU_MAX_OBSERVERS];
  alignas(8) char data[EMU_INLINE_MAX];
};

// Bulk message - supports managed payloads
struct EmuBulkMessage {
  uint16_t topicId;
  uint8_t observerCount;
  bool isInline;
  size_t payloadSize;
  EmuObserver observers[EMU_MAX_OBSERVERS];
  union {
    RefCountedPayload *managed;
    alignas(8) char inlineData[EMU_INLINE_MAX];
  };
};

template <size_t FastCapacity = 4096, size_t BulkCapacity = 16384>
class EventQueueEmulator {
  static_assert((FastCapacity & (FastCapacity - 1)) == 0, "FastCapacity must be power of 2");
  static_assert((BulkCapacity & (BulkCapacity - 1)) == 0, "BulkCapacity must be power of 2");

  // Separate queues for different priorities
  MPMCQueue<EmuFastMessage *, FastCapacity> m_fastQueue;
  MPMCQueue<EmuBulkMessage *, BulkCapacity> m_bulkQueue;

  ObjectPoolLockFree<EmuFastMessage> m_fastPool;
  ObjectPoolLockFree<EmuBulkMessage> m_bulkPool;

  struct TopicInfo {
    TopicPriority priority;
    std::atomic<uint8_t> observerCount{0};
    EmuObserver observers[EMU_MAX_OBSERVERS];
  };

  alignas(64) TopicInfo m_topics[EMU_MAX_TOPICS];
  std::atomic<uint16_t> m_topicCount{0};

public:
  EventQueueEmulator()
      : m_fastPool(256, 2048), m_bulkPool(128, 4096) {}

  // Register topic with priority
  uint16_t registerTopic(TopicPriority priority = TopicPriority::Normal) {
    uint16_t id = m_topicCount.fetch_add(1, std::memory_order_relaxed);
    if (id >= EMU_MAX_TOPICS) {
      m_topicCount.fetch_sub(1, std::memory_order_relaxed);
      return UINT16_MAX;
    }
    m_topics[id].priority = priority;
    return id;
  }

  bool addObserver(uint16_t topicId, EmuCallback callback, void *userData = nullptr) {
    if (topicId >= EMU_MAX_TOPICS)
      return false;

    TopicInfo &topic = m_topics[topicId];
    uint8_t idx = topic.observerCount.load(std::memory_order_acquire);
    if (idx >= EMU_MAX_OBSERVERS)
      return false;

    topic.observers[idx].callback = callback;
    topic.observers[idx].userData = userData;
    topic.observerCount.store(idx + 1, std::memory_order_release);
    return true;
  }

  // Fast post for critical events - inline only, fails if payload > 48B
  bool postFast(uint16_t topicId, const void *data, size_t size) {
    if (topicId >= EMU_MAX_TOPICS || size > EMU_INLINE_MAX)
      return false;

    TopicInfo &topic = m_topics[topicId];
    uint8_t count = topic.observerCount.load(std::memory_order_acquire);
    if (count == 0)
      return true;

    EmuFastMessage *msg = m_fastPool.acquire();
    msg->topicId = topicId;
    msg->observerCount = count;
    msg->payloadSize = static_cast<uint8_t>(size);

    std::memcpy(msg->observers, topic.observers, count * sizeof(EmuObserver));
    if (data && size > 0)
      std::memcpy(msg->data, data, size);

    while (!m_fastQueue.try_push(msg)) {
      std::this_thread::yield();
    }
    return true;
  }

  // Convenience for POD types on fast queue
  template <typename T>
  bool postFastValue(uint16_t topicId, const T &value) {
    static_assert(sizeof(T) <= EMU_INLINE_MAX, "Type too large for fast queue");
    static_assert(std::is_trivially_copyable<T>::value, "T must be POD");
    return postFast(topicId, &value, sizeof(T));
  }

  // Signal-only post (no payload)
  bool postFast(uint16_t topicId) {
    return postFast(topicId, nullptr, 0);
  }

  // Bulk post - for larger payloads, auto-selects inline vs managed
  bool postBulk(uint16_t topicId, const void *data, size_t size) {
    if (topicId >= EMU_MAX_TOPICS)
      return false;

    TopicInfo &topic = m_topics[topicId];
    uint8_t count = topic.observerCount.load(std::memory_order_acquire);
    if (count == 0)
      return true;

    EmuBulkMessage *msg = m_bulkPool.acquire();
    msg->topicId = topicId;
    msg->observerCount = count;
    msg->payloadSize = size;

    std::memcpy(msg->observers, topic.observers, count * sizeof(EmuObserver));

    if (size <= EMU_INLINE_MAX) {
      msg->isInline = true;
      if (data && size > 0)
        std::memcpy(msg->inlineData, data, size);
    } else {
      msg->isInline = false;
      msg->managed = RefCountedPayload::create(data, size);
      if (count > 1)
        msg->managed->addRefs(count - 1);
    }

    while (!m_bulkQueue.try_push(msg)) {
      std::this_thread::yield();
    }
    return true;
  }

  // Auto-routing post based on topic priority
  bool post(uint16_t topicId, const void *data, size_t size) {
    if (topicId >= EMU_MAX_TOPICS)
      return false;

    if (m_topics[topicId].priority == TopicPriority::Critical) {
      if (size <= EMU_INLINE_MAX)
        return postFast(topicId, data, size);
      return false;  // Critical topics can't have large payloads
    }
    return postBulk(topicId, data, size);
  }

  // Dispatch one fast message (returns true if dispatched)
  bool dispatchFast() {
    EmuFastMessage *msg = nullptr;
    if (!m_fastQueue.try_pop(msg) || !msg)
      return false;

    for (uint8_t i = 0; i < msg->observerCount; ++i) {
      msg->observers[i].callback(msg->topicId, msg->data, msg->payloadSize,
                                  msg->observers[i].userData);
    }

    m_fastPool.release(msg);
    return true;
  }

  // Dispatch one bulk message
  bool dispatchBulk() {
    EmuBulkMessage *msg = nullptr;
    if (!m_bulkQueue.try_pop(msg) || !msg)
      return false;

    const void *data = msg->isInline ? msg->inlineData
                                      : (msg->managed ? msg->managed->data() : nullptr);

    for (uint8_t i = 0; i < msg->observerCount; ++i) {
      msg->observers[i].callback(msg->topicId, data, msg->payloadSize,
                                  msg->observers[i].userData);

      if (!msg->isInline && msg->managed)
        msg->managed->release();
    }

    m_bulkPool.release(msg);
    return true;
  }

  // Priority dispatch: drain fast queue first, then one bulk
  // Returns number of messages dispatched
  size_t dispatchPriority() {
    size_t count = 0;

    // Always drain all critical messages first
    while (dispatchFast())
      ++count;

    // Then process one bulk message
    if (dispatchBulk())
      ++count;

    return count;
  }

  // Dispatch all with priority (fast first, then bulk)
  size_t dispatchAll() {
    size_t count = 0;

    // Drain fast queue completely
    while (dispatchFast())
      ++count;

    // Then drain bulk queue
    while (dispatchBulk())
      ++count;

    return count;
  }

  // Ratio-based dispatch: N fast per 1 bulk (prevents bulk starvation)
  size_t dispatchRatio(size_t fastPerBulk = 4) {
    size_t count = 0;
    size_t fastCount = 0;

    while (true) {
      // Try fast messages up to ratio
      if (fastCount < fastPerBulk && dispatchFast()) {
        ++count;
        ++fastCount;
        continue;
      }

      // Try one bulk
      if (dispatchBulk()) {
        ++count;
        fastCount = 0;  // Reset ratio
        continue;
      }

      // Try fast if bulk was empty
      if (dispatchFast()) {
        ++count;
        ++fastCount;
        continue;
      }

      break;  // Both queues empty
    }

    return count;
  }

  bool fastEmpty() const { return m_fastQueue.empty(); }
  bool bulkEmpty() const { return m_bulkQueue.empty(); }
  bool empty() const { return fastEmpty() && bulkEmpty(); }

  size_t fastQueueSize() const { return m_fastQueue.size_approx(); }
  size_t bulkQueueSize() const { return m_bulkQueue.size_approx(); }
};

// Pre-defined topic IDs for common emulator events
namespace EmuTopics {
  constexpr uint16_t VBLANK = 0;
  constexpr uint16_t AUDIO_SYNC = 1;
  constexpr uint16_t INPUT_KEY = 2;
  constexpr uint16_t INPUT_JOY = 3;
  constexpr uint16_t FRAME_READY = 4;
  constexpr uint16_t CPU_TRACE = 5;
  constexpr uint16_t MEM_TRACE = 6;
  constexpr uint16_t STATE_SAVE = 7;
  constexpr uint16_t SCREENSHOT = 8;
}

#endif // MESSAGE_CENTER_EVENTQUEUE_EMULATOR_H
