#pragma once

/// @file ttddeltabase.h
/// @brief The engine's delta base: every region's latest captured contents, the base each new difference is
/// computed against (TimeTravelEngine::Capture).
///
/// Sparse: a piece filled with one byte value (all zeros, an erased flash's #FF) keeps only that value, and its
/// bytes come from one shared page per value; only the other pieces hold a 4 KB copy. Large device memories are
/// mostly such pieces (NeoGS RAM and flash, MoonSound wave memory, VDAC2 RAM_G), so the base costs what the
/// recorded memories hold rather than their sizes. A piece never set reads as zeros.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "debugger/ttd/engine/ttdregion.h"

namespace ttd
{

class TTDDeltaBase
{
public:
    /// Drop every region's contents and keep `regions` empty ones
    void Reset(size_t regions)
    {
        _regions.clear();
        _regions.resize(regions);
        _storedPieces = 0;
    }

    /// The piece's bytes as last set (kTTDPieceSize; zeros for a piece never set). Valid until the piece is set
    /// again or the base is reset
    const uint8_t* Get(uint32_t region, uint32_t piece)
    {
        const Region& r = _regions[region];
        if (piece < r.pieces.size() && r.pieces[piece])
            return r.pieces[piece].get();
        return UniformPage(piece < r.fill.size() ? r.fill[piece] : 0);
    }

    /// Whether the piece holds exactly `bytes`
    bool Equals(uint32_t region, uint32_t piece, const uint8_t* bytes)
    {
        return std::memcmp(Get(region, piece), bytes, kTTDPieceSize) == 0;
    }

    /// Make `bytes` the piece's contents. Returns the bytes copied: 0 for a uniform piece (kept as its value)
    size_t Set(uint32_t region, uint32_t piece, const uint8_t* bytes)
    {
        Region& r = _regions[region];
        if (piece >= r.pieces.size())
        {
            r.pieces.resize(size_t(piece) + 1);
            r.fill.resize(size_t(piece) + 1, 0);
        }
        std::unique_ptr<uint8_t[]>& stored = r.pieces[piece];
        if (IsUniform(bytes))
        {
            r.fill[piece] = bytes[0];
            if (stored)
            {
                stored.reset();
                --_storedPieces;
            }
            return 0;
        }
        if (!stored)
        {
            stored.reset(new uint8_t[kTTDPieceSize]);
            ++_storedPieces;
        }
        if (stored.get() != bytes)
            std::memcpy(stored.get(), bytes, kTTDPieceSize);
        return kTTDPieceSize;
    }

    /// Pieces holding a copy of their own
    size_t StoredPieces() const { return _storedPieces; }

    /// Heap in use: the stored pieces, the shared uniform pages and the per-piece tables
    size_t HeapBytes() const
    {
        size_t bytes = _storedPieces * kTTDPieceSize + _regions.capacity() * sizeof(Region);
        for (const Region& r : _regions)
            bytes += r.pieces.capacity() * sizeof(r.pieces[0]) + r.fill.capacity();
        for (const auto& page : _uniform)
            if (page)
                bytes += kTTDPieceSize;
        return bytes;
    }

    /// Every byte of the piece equal to its first
    static bool IsUniform(const uint8_t* bytes)
    {
        return std::memcmp(bytes, bytes + 1, kTTDPieceSize - 1) == 0;
    }

private:
    struct Region
    {
        std::vector<std::unique_ptr<uint8_t[]>> pieces;   ///< null: a uniform piece
        std::vector<uint8_t> fill;                        ///< a uniform piece's value
    };

    const uint8_t* UniformPage(uint8_t value)
    {
        std::unique_ptr<uint8_t[]>& page = _uniform[value];
        if (!page)
        {
            page.reset(new uint8_t[kTTDPieceSize]);
            std::memset(page.get(), value, kTTDPieceSize);
        }
        return page.get();
    }

    std::vector<Region> _regions;
    std::array<std::unique_ptr<uint8_t[]>, 256> _uniform;   ///< one shared read-only page per value, made on use
    size_t _storedPieces = 0;
};

}  // namespace ttd
