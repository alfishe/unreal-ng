#pragma once

#ifndef MESSAGE_CENTER_PAYLOAD_REFCOUNTED_H
#define MESSAGE_CENTER_PAYLOAD_REFCOUNTED_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

// Zero-copy reference-counted payload for broadcast delivery.
// Payload is allocated once, shared across all observers, freed after last release.
// Memory layout: [RefCountedPayload header][user data...]

class RefCountedPayload {
  std::atomic<int> m_refCount{1};
  size_t m_size;
  // Use char[1] instead of flexible array member for MSVC compatibility
  // Actual allocation includes extra space for user data
  alignas(8) char m_data[1];

  RefCountedPayload(size_t size) : m_size(size) {}

public:
  // Allocate payload with space for 'size' bytes of user data
  static RefCountedPayload *create(size_t size) {
    void *mem = ::operator new(sizeof(RefCountedPayload) + size);
    return new (mem) RefCountedPayload(size);
  }

  // Allocate and copy data
  static RefCountedPayload *create(const void *data, size_t size) {
    auto *p = create(size);
    std::memcpy(p->m_data, data, size);
    return p;
  }

  // Create from typed value (POD types only)
  template <typename T>
  static RefCountedPayload *fromValue(const T &value) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "T must be trivially copyable");
    return create(&value, sizeof(T));
  }

  void addRef() { m_refCount.fetch_add(1, std::memory_order_relaxed); }

  void release() {
    if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      this->~RefCountedPayload();
      ::operator delete(this);
    }
  }

  // Increment ref count by N (for broadcast to N observers)
  void addRefs(int count) {
    m_refCount.fetch_add(count, std::memory_order_relaxed);
  }

  void *data() { return m_data; }
  const void *data() const { return m_data; }
  size_t size() const { return m_size; }

  template <typename T> T *as() { return reinterpret_cast<T *>(m_data); }

  template <typename T> const T *as() const {
    return reinterpret_cast<const T *>(m_data);
  }

  int refCount() const { return m_refCount.load(std::memory_order_relaxed); }
};

// RAII guard for automatic release
class PayloadGuard {
  RefCountedPayload *m_payload;

public:
  explicit PayloadGuard(RefCountedPayload *p) : m_payload(p) {}
  ~PayloadGuard() {
    if (m_payload)
      m_payload->release();
  }

  PayloadGuard(const PayloadGuard &) = delete;
  PayloadGuard &operator=(const PayloadGuard &) = delete;

  PayloadGuard(PayloadGuard &&other) noexcept : m_payload(other.m_payload) {
    other.m_payload = nullptr;
  }

  RefCountedPayload *get() const { return m_payload; }
  RefCountedPayload *operator->() const { return m_payload; }

  RefCountedPayload *release() {
    auto *p = m_payload;
    m_payload = nullptr;
    return p;
  }
};

#endif // MESSAGE_CENTER_PAYLOAD_REFCOUNTED_H
