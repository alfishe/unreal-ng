#pragma once

/// @file ttd_page_store.h
/// @brief Copy-on-Write page store for TTD checkpoint RAM capture.
///
/// Per parent TDD §6.3 and §6.5:
///   - A fixed-structure pool of 16 KB slots, each refcounted.
///   - Checkpoint N+1 copies only pages dirty since checkpoint N; clean
///     pages share the previous checkpoint's slot via AddRef (refcount++).
///   - Pages never touched in the session carry the NEVER_TOUCHED sentinel
///     in TTDPageRef and never enter the store at all (zero cost).
///   - The store grows on demand; budget enforcement (default 64 MB, §6.5)
///     is the capture orchestrator's responsibility — it checks
///     GetCapacityBytes() after each frame and thins oldest-densest first.
///
/// Thread model: touched only from the emulator thread during capture
/// (Intern / AddRef) and from the control thread during thinning/seek
/// (Release / GetPage) while the emulator is paused. No internal locking.
///
/// Slot lifecycle:
///   - Intern() allocates a slot (from the free list or by growing), copies
///     16 KB into it, sets refcount = 1, returns the index.
///   - AddRef() increments the refcount. Used when a new checkpoint shares
///     a clean page with the previous checkpoint.
///   - Release() decrements the refcount. When it reaches 0, the slot is
///     pushed onto the free list (available for reuse by a future Intern).
///   - GetPage() returns a read-only pointer for the restore path.

#include <cstdint>
#include <cstddef>
#include <vector>

#include "emulator/platform.h"  // PAGE_SIZE

namespace ttd {

class TTDPageStore
{
public:
    /// Size of one page slot. Matches the emulator's physical RAM page size.
    static constexpr size_t kPageSize = PAGE_SIZE;  // 16 384 bytes

    TTDPageStore() = default;
    ~TTDPageStore() = default;

    TTDPageStore(const TTDPageStore&) = delete;
    TTDPageStore& operator=(const TTDPageStore&) = delete;

    /// @brief Copy a 16 KB page into a free slot and return its index.
    ///
    /// If no free slot exists, the backing storage grows by one slot. The
    /// returned index is stable until Release() frees it and a subsequent
    /// Intern() reuses the slot.
    ///
    /// @param pageData  Source buffer, exactly kPageSize bytes. Must not be null.
    /// @return Slot index (use in TTDPageRef::storeIndex).
    uint32_t Intern(const uint8_t* pageData);

    /// @brief Increment the refcount of an existing slot.
    ///
    /// Used when a new checkpoint shares a clean page with the previous
    /// checkpoint: the checkpoint stores the same storeIndex and AddRef's it
    /// instead of paying for a second Intern copy.
    ///
    /// @param idx  Slot index previously returned by Intern.
    void AddRef(uint32_t idx);

    /// @brief Decrement the refcount of a slot.
    ///
    /// When the refcount reaches zero, the slot is pushed onto the free list
    /// and becomes eligible for reuse by a future Intern. The backing memory
    /// is not shrunk (amortised growth; see GetCapacityBytes vs GetUsedBytes).
    ///
    /// @param idx  Slot index previously returned by Intern.
    void Release(uint32_t idx);

    /// @brief Read-only access to a stored page (restore path).
    ///
    /// The pointer is stable as long as the slot is live (not freed by a
    /// Release that brought refcount to zero) and the store is not Reset.
    ///
    /// @param idx  Slot index previously returned by Intern.
    /// @return Pointer to kPageSize bytes of page data.
    const uint8_t* GetPage(uint32_t idx) const;

    /// @brief Current refcount of a slot. Diagnostic / test helper.
    uint32_t GetRefCount(uint32_t idx) const;

    // --- Capacity / memory tracking (used by the capture orchestrator for
    //     budget enforcement per TDD §6.5) ---

    /// @brief Number of slots currently allocated (capacity).
    inline uint32_t GetCapacity() const { return static_cast<uint32_t>(_refcounts.size()); }

    /// @brief Number of slots with refcount > 0 (in use).
    inline uint32_t GetUsedSlots() const { return _usedSlots; }

    /// @brief Total bytes allocated for page backing storage (capacity × page).
    /// This is the number to compare against the session budget (§6.5 default 64 MB).
    inline size_t GetCapacityBytes() const { return _pages.size(); }

    /// @brief Bytes currently in use by live slots (usedSlots × page).
    inline size_t GetUsedBytes() const { return static_cast<size_t>(_usedSlots) * kPageSize; }

    /// @brief Number of slots on the free list (available for immediate reuse).
    inline uint32_t GetFreeSlotCount() const { return static_cast<uint32_t>(_freeList.size()); }

    /// @brief Clear all storage and reset counters. Used by session
    /// invalidation (TDD §4.2: Reset / Load* / speed change / debugger edits).
    void Reset();

private:
    /// Flat backing storage: capacity × kPageSize bytes.
    /// Grown by kPageSize on each capacity increase.
    std::vector<uint8_t> _pages;

    /// One refcount per slot. A slot is "free" when its refcount is 0 and
    /// its index is on _freeList.
    std::vector<uint32_t> _refcounts;

    /// LIFO free list of slot indices available for reuse by Intern.
    std::vector<uint32_t> _freeList;

    /// Number of slots with refcount > 0. Maintained incrementally:
    /// Intern increments, Release-to-zero decrements.
    uint32_t _usedSlots = 0;
};

} // namespace ttd
