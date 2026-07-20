/// @file ttd_page_store.cpp
/// @brief TTDPageStore non-inline helpers.
///
/// Per parent TDD §6.3 and §6.5. The store is a simple refcounted pool of
/// fixed-size (16 KB) slots. Growth is amortised O(1): each Intern either
/// reuses a freed slot or appends one slot to the backing vectors.

#include "ttd_page_store.h"

#include <cassert>
#include <cstring>  // memcpy

namespace ttd
{

uint32_t TTDPageStore::Intern(const uint8_t* pageData)
{
    assert(pageData != nullptr);

    uint32_t idx;

    if (!_freeList.empty())
    {
        // Reuse a previously freed slot
        idx = _freeList.back();
        _freeList.pop_back();
    }
    else
    {
        // Grow by one slot
        idx = static_cast<uint32_t>(_refcounts.size());
        _pages.resize(static_cast<size_t>(idx + 1) * kPageSize);
        _refcounts.push_back(0);  // Will be set to 1 below
    }

    // Copy the page data into the slot
    std::memcpy(&_pages[static_cast<size_t>(idx) * kPageSize], pageData, kPageSize);

    _refcounts[idx] = 1;
    _usedSlots++;

    return idx;
}

void TTDPageStore::AddRef(uint32_t idx)
{
    assert(idx < _refcounts.size());
    assert(_refcounts[idx] > 0);  // Can't AddRef a free slot

    _refcounts[idx]++;
}

void TTDPageStore::Release(uint32_t idx)
{
    assert(idx < _refcounts.size());
    assert(_refcounts[idx] > 0);  // Double-release guard

    _refcounts[idx]--;

    if (_refcounts[idx] == 0)
    {
        // Slot is now free — push onto the free list for reuse
        _freeList.push_back(idx);
        _usedSlots--;
    }
}

const uint8_t* TTDPageStore::GetPage(uint32_t idx) const
{
    assert(idx < _refcounts.size());
    assert(_refcounts[idx] > 0);  // Don't read freed data

    return &_pages[static_cast<size_t>(idx) * kPageSize];
}

uint32_t TTDPageStore::GetRefCount(uint32_t idx) const
{
    assert(idx < _refcounts.size());
    return _refcounts[idx];
}

void TTDPageStore::Reset()
{
    _pages.clear();
    _pages.shrink_to_fit();
    _refcounts.clear();
    _refcounts.shrink_to_fit();
    _freeList.clear();
    _freeList.shrink_to_fit();
    _usedSlots = 0;
}

} // namespace ttd
