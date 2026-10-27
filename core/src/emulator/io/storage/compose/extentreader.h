#pragma once

/// @file extentreader.h
/// @brief The read hot path shared by every synthesized volume: sector n of a
/// file goes straight from where the file lives (a host file or an extent of a
/// source device) into the caller's buffer. No intermediate buffer, no heap
/// allocation per read (NFR-P3); sequential reads find their extent in O(1)
/// through a last-hit cache, others by binary search (NFR-P4).
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §4.1.

#include <cstdint>
#include <vector>

#include "emulator/io/storage/compose/filetree.h"

class SourcePool;

class ExtentReader
{
public:
    static constexpr uint32_t kSectorSize = 512;

    ExtentReader(SourcePool& pool, const std::vector<Extent>& extents) : _pool(pool), _extents(extents) {}

    /// Read sector `fileSector` of the file into `dst` (512 bytes). Past the
    /// end of the file, and in the last sector past the file's size, `dst`
    /// holds zeros. False on a device read error
    bool ReadFileSector(const FileData& data, uint64_t fileSector, uint8_t* dst);

    /// Binary searches done so far (tests: sequential reads do none)
    uint64_t Searches() const { return _searches; }

private:
    const Extent* FindExtent(const FileData& data, uint64_t fileSector);

    SourcePool& _pool;
    const std::vector<Extent>& _extents;
    const FileData* _lastData = nullptr;
    uint32_t _lastExtent = 0;
    uint64_t _searches = 0;
};
