#pragma once

/// @file ttdseekindex.h
/// @brief The seek index of a recording folder's segment file (TTDSessionFile::ReadSegmentWindow): where each
/// part's records lie, so a restore of one checkpoint reads the part of the file it needs. Format:
/// docs/emulator/design/debugger/time-travel-debug/ttd-container-format.md, core/src/debugger/ttd/ttd.ksy
/// (seek_index, payload_piece_tables).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ttd
{

/// One part of a recording folder's segment file in its seek index: where its piece, checkpoint and piece-table
/// records lie (the offsets of their record headers; 0: the part has none) and how many bytes each stores, the
/// number of its first version in the file, and the frames of the checkpoints its table record holds a table of
struct TTDSeekIndexPart
{
    uint64_t piecesOffset = 0;
    uint64_t checkpointsOffset = 0;
    uint64_t tablesOffset = 0;
    uint32_t piecesStored = 0;
    uint32_t checkpointsStored = 0;
    uint32_t tablesStored = 0;
    uint64_t firstItem = 0;
    uint32_t items = 0;   ///< the versions its piece record holds
    uint64_t firstFrame = 0;
    std::vector<uint64_t> tableFrames;   ///< ascending
};

/// The seek index of a recording folder's segment file: the container's index extra. With it a restore of one
/// checkpoint reads the trailer, the index, one table record and the piece and checkpoint records of the parts it
/// needs, instead of every record from the file's start (P1.S10 seek format, 2026-11-23). It names the offset the
/// container's index starts at: a copy of the file whose records moved (a join, a part dropped) fails that check
/// and is read as a file without a seek index.
///
/// Worked example: a segment of 1,875 frames in 38 parts of 50 checkpoints, full tables every 64 checkpoints. A
/// seek to its frame 1,000 finds the table of frame 960 in part 19, reads the checkpoints of parts 19 and 20
/// (frames 960-1,024), the piece records of those parts and of the earlier parts holding a version the table or a
/// difference chain names (part 0 for the pieces unchanged since the baseline)
struct TTDSeekIndex
{
    uint32_t line = 0;   ///< the file's line as its parts name it (part_end.branch); 0 the trunk
    std::vector<TTDSeekIndexPart> parts;

    std::vector<uint8_t> Encode(uint64_t indexOffset) const;
    /// False for anything but a seek index of a file whose index starts at @p indexOffset
    static bool Decode(const uint8_t* data, size_t size, uint64_t indexOffset, TTDSeekIndex& out);
};

/// Buffers TTDSessionFile::ReadSegmentWindow reuses from one read to the next (a read back allocates nothing once
/// they have grown)
struct TTDSegmentWindowScratch
{
    std::vector<uint8_t> extra;
    TTDSeekIndex index;
    std::vector<uint8_t> stored;
    std::vector<uint8_t> record;
    std::vector<std::vector<uint8_t>> pieces;   ///< per part: its piece record
    std::vector<uint8_t> wanted;
    std::vector<uint32_t> wantedInPart;
    std::vector<uint32_t> idOf;
    /// What the last read did (a measurement's breakdown)
    struct Work
    {
        uint32_t partsRead = 0;        ///< piece records read
        uint32_t versionsImported = 0;
        uint64_t bytesRead = 0;        ///< stored bytes of every record read
    } work;
};

}  // namespace ttd
