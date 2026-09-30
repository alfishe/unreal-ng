#pragma once

/// @file rzxformat.h
/// @brief The RZX input-recording file as data: the blocks of an RZX 0.12 /
/// 0.13 file after parsing (docs/inprogress/2026-09-29-rzx-replay/design.md
/// §3). No emulator dependency: the player (emulator/rzx/) consumes it.
///
/// An RZX file is a start snapshot plus, for every interrupt interval of the
/// recorded session ("frame"), the number of opcode fetches the CPU made and
/// the value of every IN it executed. Worked example: a frame {fetchCount
/// 4000, IN values [#BF, #BF, #FE]} means "after 4000 R-register increments
/// the next interrupt comes; the three INs in between return #BF, #BF, #FE".
/// A frame with IN counter 65535 repeats the previous frame's IN values.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rzx
{
    /// "RZX!" + major + minor + flags
    constexpr size_t kHeaderSize = 10;
    /// Block id + block length (the length includes these 5 bytes)
    constexpr size_t kBlockHeaderSize = 5;

    constexpr uint8_t kBlockCreator = 0x10;
    constexpr uint8_t kBlockSecurityInfo = 0x20;
    constexpr uint8_t kBlockSecuritySignature = 0x21;
    constexpr uint8_t kBlockSnapshot = 0x30;
    constexpr uint8_t kBlockInput = 0x80;

    /// Input block header: id, length, frame count, reserved byte, T-states, flags
    constexpr size_t kInputHeaderSize = 18;
    /// Snapshot block header: id, length, flags, extension, uncompressed length
    constexpr size_t kSnapshotHeaderSize = 17;
    /// Creator block: id, length, ASCIIZ[20] name, major, minor
    constexpr size_t kCreatorHeaderSize = 29;

    constexpr uint32_t kSnapshotFlagExternal = 0x01;
    constexpr uint32_t kSnapshotFlagCompressed = 0x02;
    constexpr uint32_t kInputFlagProtected = 0x01;
    constexpr uint32_t kInputFlagCompressed = 0x02;
    constexpr uint32_t kFileFlagSigned = 0x01;

    /// IN counter of a frame that repeats the previous frame's IN values
    constexpr uint16_t kRepeatFrame = 0xFFFF;

    /// Decompression bounds (no decompression bombs): a snapshot is at most a
    /// few hundred KiB (Pentagon 1024 SZX: 64 pages); an input block of one
    /// hour at 50 frames/s with 50 INs per frame is about 9 MiB
    constexpr size_t kMaxSnapshotSize = 16u * 1024u * 1024u;
    constexpr size_t kMaxInputBlockSize = 256u * 1024u * 1024u;

    struct Creator
    {
        std::string name;  ///< e.g. "Fuse", "Spectaculator 80.3092"
        uint16_t major = 0;
        uint16_t minor = 0;
    };

    struct Snapshot
    {
        std::string extension;         ///< lower case: "z80", "sna", "szx"
        bool compressed = false;       ///< stored zlib-compressed in the file
        bool external = false;         ///< a descriptor instead of the image
        uint32_t externalChecksum = 0; ///< external only
        std::string externalName;      ///< external only: the file name as stored
        std::vector<uint8_t> data;     ///< embedded only: the image, uncompressed
    };

    /// One interrupt interval. Repeat frames are resolved at parse time: they
    /// point at the previous frame's IN values (inOffset / inCount), so the
    /// player never looks back
    struct Frame
    {
        uint16_t fetchCount = 0;  ///< R increments until the interrupt (acknowledge excluded)
        uint16_t inCount = 0;     ///< number of IN values (never 65535 here)
        uint32_t inOffset = 0;    ///< first value in InputBlock::inValues
        bool repeated = false;    ///< stored as a repeat of the previous frame
    };

    struct InputBlock
    {
        uint32_t tstates = 0;          ///< T-state counter at the block start (from the recording INT)
        bool compressed = false;
        bool protectedFrames = false;  ///< encrypted frames: not parsed, refused by the player
        std::vector<Frame> frames;
        std::vector<uint8_t> inValues;
    };

    enum class BlockType : uint8_t
    {
        Snapshot,
        Input
    };

    /// Snapshots and input blocks in file order: several input blocks separated
    /// by snapshots record a multiload game or a rollback point
    struct BlockRef
    {
        BlockType type = BlockType::Input;
        size_t index = 0;  ///< into File::snapshots or File::inputs
    };

    struct File
    {
        uint8_t major = 0;
        uint8_t minor = 0;
        bool isSigned = false;  ///< header flag: a signature covers the data

        bool hasCreator = false;
        Creator creator;

        bool hasSecurityInfo = false;
        uint32_t keyId = 0;
        uint32_t weekCode = 0;
        bool hasSignature = false;

        std::vector<Snapshot> snapshots;
        std::vector<InputBlock> inputs;
        std::vector<BlockRef> order;
        std::vector<std::string> warnings;  ///< tolerated oddities (unknown blocks, trailing bytes)

        uint64_t TotalFrames() const;
        /// "0.13"
        std::string VersionText() const;
        /// A multi-line summary: creator, version, blocks, frames
        std::string Describe() const;
    };

    /// Inflate a zlib stream (RFC 1950). `expected` > 0: the output must be
    /// exactly that long; 0: any length up to `limit`. False on a corrupt
    /// stream or a size violation
    bool Inflate(const uint8_t* data, size_t size, size_t expected, size_t limit, std::vector<uint8_t>& out);
    /// Deflate into a zlib stream; empty on failure
    std::vector<uint8_t> Deflate(const uint8_t* data, size_t size);
}  // namespace rzx
