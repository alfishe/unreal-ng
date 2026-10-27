#pragma once

/// @file changeview.h
/// @brief A change layer's sectors, read without knowing where they are kept: a session keeps them in memory and in
/// a spill file (C10d), a test in a plain map. In sector order, with the data of each.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c10d-session-spill.md §3.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>

#include "emulator/io/storage/iblockdevice.h"

class IChangeView
{
public:
    virtual ~IChangeView() = default;

    /// Sectors that differ from the layer's base
    virtual size_t ChangedSectors() const = 0;
    /// The first changed sector at or after `lba`
    virtual std::optional<uint64_t> NextChanged(uint64_t lba) const = 0;
    /// A changed sector's data (false: not changed, or it cannot be read)
    virtual bool ReadChanged(uint64_t lba, uint8_t* dst) const = 0;

    /// Is any sector in [first, first + count) changed
    bool ChangedIn(uint64_t first, uint64_t count) const
    {
        const std::optional<uint64_t> next = NextChanged(first);
        return next && *next < first + count;
    }

    /// Every change in sector order; `visit` returns false to stop. False when stopped or a sector cannot be read
    bool ForEachChange(const std::function<bool(uint64_t lba, const uint8_t* data)>& visit) const
    {
        uint8_t data[IBlockDevice::kSectorSize];
        for (std::optional<uint64_t> lba = NextChanged(0); lba; lba = NextChanged(*lba + 1))
        {
            if (!ReadChanged(*lba, data) || !visit(*lba, data))
                return false;
        }
        return true;
    }
};

/// A plain map of changed sectors as a view (tests, callers that built their own set)
class MapChangeView : public IChangeView
{
public:
    using Map = std::map<uint64_t, std::array<uint8_t, IBlockDevice::kSectorSize>>;

    explicit MapChangeView(const Map& changes) : _changes(changes) {}

    size_t ChangedSectors() const override { return _changes.size(); }
    std::optional<uint64_t> NextChanged(uint64_t lba) const override
    {
        const auto it = _changes.lower_bound(lba);
        return it == _changes.end() ? std::nullopt : std::optional<uint64_t>(it->first);
    }
    bool ReadChanged(uint64_t lba, uint8_t* dst) const override
    {
        const auto it = _changes.find(lba);
        if (it == _changes.end())
            return false;
        std::memcpy(dst, it->second.data(), IBlockDevice::kSectorSize);
        return true;
    }

private:
    const Map& _changes;
};

/// The changes inside [start, start + sectors) of another view, numbered from `start` (a partition's window)
class WindowChangeView : public IChangeView
{
public:
    WindowChangeView(const IChangeView& inner, uint64_t start, uint64_t sectors) : _inner(inner), _start(start), _sectors(sectors)
    {
        for (std::optional<uint64_t> lba = NextChanged(0); lba; lba = NextChanged(*lba + 1))
            _count++;
    }

    size_t ChangedSectors() const override { return _count; }
    std::optional<uint64_t> NextChanged(uint64_t lba) const override
    {
        if (lba >= _sectors)
            return std::nullopt;
        const std::optional<uint64_t> next = _inner.NextChanged(_start + lba);
        if (!next || *next >= _start + _sectors)
            return std::nullopt;
        return *next - _start;
    }
    bool ReadChanged(uint64_t lba, uint8_t* dst) const override { return lba < _sectors && _inner.ReadChanged(_start + lba, dst); }

private:
    const IChangeView& _inner;
    uint64_t _start;
    uint64_t _sectors;
    size_t _count = 0;
};
