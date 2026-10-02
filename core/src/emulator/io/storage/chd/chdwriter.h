#pragma once

/// @file chdwriter.h
/// @brief Writing a CHD v5 file: uncompressed (MAME's writable hard-disk form)
/// or compressed with up to four codecs, optionally as a child of a parent CHD.
///
/// | Kind | Layout | Hunks |
/// |---|---|---|
/// | uncompressed | header, map (32-bit hunk numbers), metadata, hunks aligned to the hunk size | zero hunks (or, in a child, hunks equal to the parent's) are not stored |
/// | compressed | header, metadata, hunks back to back, Huffman-coded map at the end | each hunk takes the codec giving the fewest bytes, else is stored; a hunk equal to an earlier one is a self reference, equal to the parent's a parent reference |
///
/// A compressed file carries the SHA-1 of its data and the overall SHA-1
/// (data plus checksummed metadata), as chdman writes them; an uncompressed one
/// leaves both zero, as MAME does (it is written in place). `chdman verify`
/// and `chdman info` accept both (docs/inprogress/2026-10-02-media-chd/design.md).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/chd/chdcodec.h"
#include "emulator/io/storage/chd/chdfile.h"
#include "emulator/io/storage/iblockdevice.h"

namespace chd
{
    struct WriteOptions
    {
        CodecList codecs{};              ///< all kCodecNone: an uncompressed CHD
        uint32_t hunkBytes = 4096;       ///< chdman's hard-disk default (8 sectors)
        uint32_t unitBytes = 512;        ///< the sector size
        std::vector<MetadataEntry> metadata;  ///< written in this order
        ChdFile* parent = nullptr;       ///< write a child of this CHD

        /// A save: hunks the guest did not change keep the stored bytes of
        /// `reuse` when it has the same hunk size and codecs (no second compression)
        ChdFile* reuse = nullptr;
        std::function<bool(uint32_t hunk)> unchanged;
    };

    /// Hunk `hunk` of the disk: hunkBytes bytes, zero past the logical end
    using HunkReader = std::function<bool(uint32_t hunk, uint8_t* dst, std::string* error)>;

    /// Write `logicalBytes` of disk to `path` (created or replaced). On failure
    /// the partial file is removed and `error` says why
    bool WriteChd(const std::string& path, uint64_t logicalBytes, const HunkReader& read, const WriteOptions& options,
                  std::string* error = nullptr);

    /// Write a whole block device (its 512-byte sectors in order)
    bool WriteChd(const std::string& path, IBlockDevice& device, const WriteOptions& options, std::string* error = nullptr);

    /// The `GDDD` hard-disk entry: "CYLS:c,HEADS:h,SECS:s,BPS:512" and a NUL, checksummed
    MetadataEntry HardDiskMetadata(const BlockGeometry& geometry, uint32_t sectorBytes = 512);
    /// Parse a `GDDD` text; false when it is not one
    bool ParseHardDiskMetadata(const std::string& text, BlockGeometry& geometry, uint32_t& sectorBytes);
    /// chdman's guess for a disk with no geometry: the most sectors per track
    /// (63 down to 2) and heads (16 down to 2) that divide the sector count.
    /// nullopt when none does (a prime count): the caller picks a geometry that
    /// does not cover the whole disk
    std::optional<BlockGeometry> GuessGeometry(uint64_t sectors);
}  // namespace chd
