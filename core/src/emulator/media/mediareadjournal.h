#pragma once

/// @file mediareadjournal.h
/// @brief Every sector a machine reads from a medium's image, recorded while
/// time travel records and handed back while it replays (Phase 3, owner
/// decision 2026-10-03: the read boundary for SD cards and hard disks is the
/// sector read). One tap per block medium covers every path - the CPU's IN,
/// TS-Conf's DMA, the NeoGS card's own CPU - and a replay needs no image file.

#include <cstddef>
#include <cstdint>
#include <string>

class IMediaReadJournal
{
public:
    virtual ~IMediaReadJournal() = default;
    /// Replaying: the bytes recorded for this read, true; false when the
    /// recording has no such read here (the read then goes to the image)
    virtual bool Playing() const = 0;
    virtual bool Play(const std::string& slot, uint64_t lba, uint8_t* out, size_t size) = 0;
    /// Recording: a sector read from the image
    virtual void Record(const std::string& slot, uint64_t lba, const uint8_t* bytes, size_t size) = 0;
};
