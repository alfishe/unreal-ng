#pragma once

// Snapshot files as containers of RAM (asm-synchronizer.md part B, Z1): .sna (48K, 128K), .z80 (v1, v2, v3) and .szx
// read into their RAM pages, the paging and SP, and written back with some pages replaced and every other byte of the
// file kept. That is what the synchronizer needs to read an assembler's text out of a snapshot and to put a host
// source into one, without an emulator. The emulator has its own snapshot pipeline; this is for the library's tools,
// tests and standalone users.
//
// Example: a Pentagon 128 .szx with ALASM 5.09 at its prompt reads as ramPages 8, the pages 0-7, windows
// {-1, 5, 2, port7FFD & 7}, sp from Z80R; WriteSnapshot with page 6 replaced writes the same blocks with that RAMP
// block replaced by an uncompressed one.

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace unrealasm::containers
{
enum class SnapshotFormat : uint8_t
{
    Sna,
    Z80,
    Szx,
};

struct Snapshot
{
    SnapshotFormat format = SnapshotFormat::Sna;
    std::string machine;                           ///< what the file says it was made on: "48K", "128K", "Pentagon 512", ...
    int ramPages = 3;                              ///< the machine's RAM in 16 KB pages (3 for a 48K: pages 5, 2, 0)
    std::map<int, std::vector<uint8_t>> pages;     ///< RAM page -> its 16384 bytes (128K numbering; a 48K has 5, 2, 0)
    std::array<int, 4> windows{-1, -1, -1, -1};    ///< RAM page at #0000 / #4000 / #8000 / #C000; -1 = ROM
    uint8_t port7FFD = 0;                          ///< the paging port (0 on a 48K)
    uint16_t sp = 0;
    uint16_t pc = 0;
    std::vector<uint8_t> bytes;                    ///< the file as read: writing keeps every part not replaced
};

/// The snapshot in `file`. `extension` ("sna", "z80", "szx", any case, empty = by content) picks the format; an SZX is
/// known by its "ZXST" mark, an SNA by its size. False with the reason when it is none of them or broken
bool ReadSnapshot(std::span<const uint8_t> file, const std::string& extension, Snapshot& out, std::string& error);

/// `snapshot`'s file with the pages in `changed` (page -> 16384 bytes) replaced: SNA in place, Z80 v2 / v3 the page's
/// block compressed anew (v1: the whole memory), SZX the page's RAMP block uncompressed. False with the reason (a
/// page the file does not hold, a size that is not 16384)
bool WriteSnapshot(const Snapshot& snapshot, const std::map<int, std::vector<uint8_t>>& changed, std::vector<uint8_t>& out,
                   std::string& error);

/// "sna", "z80" or "szx"
std::string SnapshotFormatName(SnapshotFormat format);
/// `extension` (any case, no dot) is one of the snapshot formats
bool IsSnapshotExtension(const std::string& extension);
}  // namespace unrealasm::containers
