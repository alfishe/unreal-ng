#pragma once

#include <cstdint>

#include "emulator/io/fdc/diskimage.h"

/// @brief Emulates "floating" / "flaky" sectors used by copy-protection schemes.
///
/// A real drive's flux transitions are marginal at a physically damaged or deliberately
/// under-clocked position on the disk. Reading that spot gives a different result on different
/// revolutions: sometimes the ID Address Mark decodes as one sector number, sometimes it doesn't
/// decode at all; the data field returns different garbage bytes each pass. Protection code
/// exploits this: it reads a known-flaky spot more than once and expects to see it change,
/// distinguishing an original disk from a bit-perfect copy (which is deterministic).
///
/// The disk model already records *which* bytes are physically weak - `RawTrack::_weak`, filled
/// in by the SCP/HFE/DSK loaders and by the UDI loader through the `UDIW` trailer-chunk
/// extension (docs/inprogress/2026-09-22-udi-weak-bit-storage/design.md). FDI never will (no
/// on-disk field for it; see
/// docs/file-formats/disk-images/fdi.md). This module is the only place that turns "this byte is
/// weak" into actual varying behavior. WD1793 calls it, but only when `track->hasWeakBits()` -
/// every solid track (all FDI images, and any track with no weak bytes) is completely unaffected
/// and keeps using `DiskImage::Track::findSector` / the raw data buffer directly.
///
/// Determinism: nothing here calls `rand()`. Every decision is a hash of values that are already
/// part of the emulated, TTD-recorded state (the WD1793 index-pulse/revolution counter for ID
/// visibility, the WD1793 T-state clock for data bytes), so a TTD replay of the same instruction
/// stream reproduces the exact same "random" sequence.
class FlakySectorEmulator
{
public:
    /// Rotational search for a Type II command's target sector, revolution-aware: an ID Address
    /// Mark with a weak byte in it (see `hasWeakIdam`) is only visible on some revolutions, so a
    /// persistent search can eventually find a sector that a single-revolution search would not.
    /// Falls back to `Track::findSector`'s ordinary behavior for every non-weak candidate.
    ///
    /// @param revolution Current disk revolution (WD1793::_indexPulseCounter)
    static DiskImage::Sector* findSector(DiskImage::Track& track, int cyl, int side, uint8_t number,
                                          size_t fromOffset, size_t revolution)
    {
        const size_t count = track.sectorCount();
        if (count == 0) return nullptr;

        size_t start = 0;
        while (start < count && track.getRawSector(start)->idamOffset < fromOffset) start++;

        for (size_t n = 0; n < count; n++)
        {
            DiskImage::Sector& sector = *track.getRawSector((start + n) % count);
            if (cyl >= 0 && sector.cylinder() != static_cast<uint8_t>(cyl)) continue;
            if (side >= 0 && sector.head() != static_cast<uint8_t>(side)) continue;
            if (!isIdamVisible(track, sector, revolution)) continue;
            if (sector.number() != number) continue;
            return &sector;
        }

        return nullptr;
    }

    /// Whether a sector's ID Address Mark decodes solidly on the given revolution. Sectors with no
    /// weak byte in their IDAM (6 bytes: FE, C, H, R, N, CRC-CRC) are always visible. A weak IDAM is
    /// visible on exactly one revolution in every four - the phase is a hash of the sector's stream
    /// position, so different weak sectors float independently. Periodicity is exact (not statistical):
    /// every window of 4 consecutive revolutions contains exactly one visible one, so a search that
    /// keeps retrying cannot miss within the WD1793 datasheet's up-to-4/5-revolution ID-search window.
    static bool isIdamVisible(const DiskImage::Track& track, const DiskImage::Sector& sector, size_t revolution)
    {
        if (!hasWeakIdam(track, sector)) return true;

        uint32_t h = static_cast<uint32_t>(sector.idamOffset) * 2654435761u;
        h ^= h >> 15;
        return (static_cast<uint32_t>(revolution) + h) % 4u == 0u;
    }

    /// Overwrites `value` in place with flaky garbage when `offset` (a byte position in the track's
    /// raw stream) is marked weak. No-op otherwise - callers should still gate on `hasWeakBits()`
    /// before paying for the call, since that is the overwhelmingly common case.
    /// @param clock Emulated T-state clock (WD1793::_time) - varies the result across read attempts
    ///              while staying reproducible for a fixed instruction stream (TTD replay).
    static void mutateWeakDataByte(const DiskImage::Track& track, size_t offset, uint64_t clock, uint8_t& value)
    {
        if (!track.weakByte(offset)) return;

        uint32_t h = static_cast<uint32_t>(clock) * 2654435761u + static_cast<uint32_t>(offset) * 40503u;
        h ^= h >> 13;
        h *= 0x85ebca6bu;
        h ^= h >> 16;
        value = static_cast<uint8_t>(h);
    }

private:
    static bool hasWeakIdam(const DiskImage::Track& track, const DiskImage::Sector& sector)
    {
        for (size_t i = 0; i < 6; i++)
        {
            if (track.weakByte(sector.idamOffset + i)) return true;
        }
        return false;
    }
};
