#pragma once

/// @file ttdpayloadstore.h
/// @brief Bytes an event carries that do not fit its record (received network
/// data, the bytes of a debugger edit, a marker's reason), kept while anything
/// refers to them: the events, and the checkpoints whose device state names
/// them (a network device's buffered bytes, D5). Reference-counted.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.2

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ttd
{

/// A stored payload; id 0 = none
struct TTDPayloadRef
{
    uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(const TTDPayloadRef& o) const { return id == o.id; }
};

class TTDPayloadStore
{
public:
    /// Store @p size bytes with one reference
    TTDPayloadRef Store(const uint8_t* bytes, size_t size);
    void AddRef(TTDPayloadRef ref);
    /// Drop one reference; the bytes go with the last
    void Release(TTDPayloadRef ref);

    /// The bytes of a live payload (empty for none or a released one)
    const std::vector<uint8_t>& Bytes(TTDPayloadRef ref) const;
    uint32_t RefCount(TTDPayloadRef ref) const;

    void Clear();
    size_t LiveCount() const { return _live; }
    size_t LiveBytes() const { return _liveBytes; }

private:
    struct Entry
    {
        std::vector<uint8_t> bytes;
        uint32_t refs = 0;
    };
    std::vector<Entry> _entries;   ///< index = id - 1
    std::vector<uint32_t> _free;
    size_t _live = 0;
    size_t _liveBytes = 0;
};

}  // namespace ttd
