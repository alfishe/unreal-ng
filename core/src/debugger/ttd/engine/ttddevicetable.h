#pragma once

/// @file ttddevicetable.h
/// @brief The engine's device table: every device of the recorded machine with
/// its descriptor, checked once and put in restore order.
///
/// Built from the same peripheral registry v1 records from, so the device set
/// is defined in one place. A device restores after the devices it names in
/// TTDDeviceDescriptor::restoreAfter; ties keep v1's order (ascending blob id,
/// then type, then instance), so the engine and v1 load devices alike.
/// The set has a version that changes when the set changes (a card switched,
/// Phase 2, Step 4).
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-2-device-state-tdd.md §5.1.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdserializable.h"

namespace ttd
{

struct TTDDeviceEntry
{
    TTDDeviceDescriptor descriptor;
    TTDSerializable* device = nullptr;   ///< the live device; null for a table read from a file
};

class TTDDeviceTable
{
public:
    /// Check and adopt a device set. Errors (the table stays as it was):
    /// a key listed twice, a device that names itself or an unknown device to
    /// restore after, a cycle, a state size of 0. @return false with @p error
    bool Build(std::vector<TTDDeviceEntry> entries, std::string& error);
    void Clear();

    /// Changes whenever Build adopts a set that differs from the previous one
    uint32_t SetVersion() const { return _setVersion; }
    const std::vector<TTDDeviceEntry>& Entries() const { return _entries; }
    /// Indices into Entries(), in restore order
    const std::vector<uint32_t>& RestoreOrder() const { return _order; }
    /// The entry of @p key, or null
    const TTDDeviceEntry* Find(const TTDDeviceKey& key) const;

private:
    static bool SameSet(const std::vector<TTDDeviceEntry>& a, const std::vector<TTDDeviceEntry>& b);

    std::vector<TTDDeviceEntry> _entries;
    std::vector<uint32_t> _order;
    uint32_t _setVersion = 0;
};

}  // namespace ttd
