#pragma once

/// @file foldertapebuilder.h
/// @brief A host folder built into a tape, once, in memory.
///
/// The folder becomes a TZX image of standard-speed blocks (ID #10) that the
/// tape deck plays like any other tape. Nothing is ever written back into the
/// folder. Rules (integration-tape.md §4):
///   - one folder, top level only; the manifest's `order` first, then the rest
///     byte-wise sorted (as for disk images)
///   - Hobeta files (`*.$B`, `*.$C`, ... with a valid header): a ROM header
///     (B -> Program, D -> Number array, anything else -> Bytes) and a data
///     block, with the Hobeta header's name, start and length
///   - `.tap` / `.tzx` files: their blocks go on the tape unchanged (a TAP
///     block becomes a #10 block)
///   - other files: a header (name from the file, 10 characters; type from
///     DiskTypeMap: B -> Program, else Bytes at 32768, a 6912-byte `.scr` at
///     16384) and a data block; the manifest's `files:` overrides name, type,
///     start and the BASIC autorun line
///   - a file larger than one tape block (65 533 bytes) is skipped and reported
///   - pause after every block: the manifest's `tape.pause`, default 1000 ms
///   - capacity: one side of a C90 cassette (45 minutes) at ROM timings; a file
///     that would pass the end is skipped and reported, and placing goes on
///
/// Worked example: `boot.$B` (a 26-byte program) and `intro.scr` (6 912 bytes)
/// give four blocks: `Program: boot`, its data, `Bytes: intro` (16384,6912),
/// its data - about 5 + 1 + 2 + 1 + 5 + 1 + 43 + 1 = 59 seconds of tape (pilot
/// tones, bits and a 1 s pause after each block).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/tape/tapetypes.h"
#include "emulator/media/mediatypes.h"

class FolderTapeBuilder
{
public:
    /// One side of a C90 cassette
    static constexpr uint32_t kCassetteSideMs = 45u * 60u * 1000u;
    /// The #10 block length field is 16 bits: flag + data + checksum
    static constexpr size_t kMaxFileBytes = 0xFFFF - 2;
    static constexpr uint16_t kDefaultPauseMs = 1000;

    /// Build the tape as TZX bytes. The result's report lists every file left
    /// out and why. `capacityMs` is for tests; the medium is one C90 side
    static MediaResult BuildTzx(const std::filesystem::path& folder, std::vector<uint8_t>& tzx,
                                uint32_t capacityMs = kCassetteSideMs);

    /// Build the tape and decode it into a playable image
    static MediaResult Build(const std::filesystem::path& folder, std::unique_ptr<TapeImage>& image,
                             uint32_t capacityMs = kCassetteSideMs);
};
