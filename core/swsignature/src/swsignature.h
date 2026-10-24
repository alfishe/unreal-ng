#pragma once

/// @file swsignature.h
/// @brief Media signatures (Steam Deck client input-and-profiles.md §4, controller manager D16): recognise a title
/// after it was re-packed (TAP ↔ TZX, TRD ↔ SCL ↔ FDI ↔ UDI ↔ TD0), renamed, or written to. One library for every
/// consumer: the controller manager's game profiles (D7, H-3), the library scanners, the metadata manager, zx-meta-db.
///
/// | Media | Signature | Robust to |
/// |---|---|---|
/// | any | exact: SHA-256 of the file (`file:sha256:`), SHA-1 and MD5 on request | nothing |
/// | TAP, TZX | content: SHA-256 over the data payloads of the byte blocks in order, without the flag and checksum bytes (`tape:sha256:`); per-block hashes | TAP ↔ TZX, pauses, info blocks |
/// | TRD, SCL, FDI, UDI, TD0, hobeta with TR-DOS | file set: per catalogue entry SHA-256(name, type, start, length, data); SHA-256 of the sorted entry hashes (`trdset:sha256:`); the set itself for the fuzzy rule (match.h) | TRD ↔ SCL, sector layout, disk label; saves (fuzzy) |
/// | SNA, Z80, SZX | memory: per 256-byte RAM page (the screen #4000-#5AFF, the stack page and uniform pages left out) the first 64 bits of its SHA-256; SHA-256 over them (`mem:sha256:`); >= 90 % equal of >= 16 common pages match | snapshots of one game at other moments (best effort) |
/// | SPG, ZXP, others | exact only | — |
///
/// The parsers are the core's: TapeLoaderRegistry (TAP, TZX), unrealdisk::FloppyFormats + TrdosFileSystem (disks),
/// LoaderSNA / LoaderZ80 / SzxReader (snapshots). Nothing here touches a machine; every call is thread-safe.
///
/// @code
///   unrealng::swsignature::Signatures sig;
///   std::string error;
///   if (unrealng::swsignature::ComputeFile("Elite.tzx", sig, error))
///       use(sig.content);  // "tape:sha256:5e1c..."; sig.Keys() for a profile's match.signatures
/// @endcode

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"
#include "match.h"

class EmulatorContext;

namespace unrealng::swsignature
{

enum class MediaKind : uint8_t
{
    Unknown,   ///< no parser took it: exact only
    Tape,
    Disk,
    Snapshot,
};
const char* ToText(MediaKind kind);

/// One byte block of a tape
struct TapeBlockHash
{
    size_t index = 0;        ///< in the tape image's play order
    uint8_t flag = 0;        ///< the flag byte ($00 header, $FF data); 0 for a block without one
    bool flagByte = true;    ///< false: a raw-bit block (TZX $14) whose payload is the whole block
    size_t length = 0;       ///< payload bytes
    std::string sha256;      ///< of the payload
};

/// One live TR-DOS catalogue entry
struct DiskFile
{
    std::string name;        ///< 8 characters, padding trimmed
    char type = ' ';
    uint16_t start = 0;
    uint16_t length = 0;
    uint8_t sectors = 0;
    bool code = false;       ///< BASIC or code: match.h's code token
    size_t dataLength = 0;   ///< the bytes hashed (a BASIC file: program + variables)
    std::string dataSha256;  ///< of the file's data
    std::string entrySha256; ///< of name (8 bytes as stored), type, start, length (LE) and the data hash
    std::string catalogSha256; ///< of name, type, start, length: the entry without its data
    /// `c:<16 hex>` / `d:<16 hex>` (match.h)
    std::string Token() const;
    /// `n:<16 hex>` (match.h)
    std::string CatalogToken() const;
};

/// One 256-byte page of snapshot RAM
struct MemoryPage
{
    uint16_t bank = 0;       ///< logical 16 KB bank (128K numbering; a 48K file holds 5, 2, 0)
    uint8_t page = 0;        ///< 0..63 inside the bank
    uint64_t hash = 0;       ///< the first 64 bits of the page's SHA-256, big-endian
};

struct Options
{
    bool externalHashes = false;  ///< SHA-1 and MD5 of the file too (ZXInfo, RetroAchievements lookups)
    /// TRD and SCL read straight from their sectors (the same result, ~50x faster); false: every disk through the
    /// core's DiskImage loaders (the cross-check)
    bool sectorImages = true;
};

struct Signatures
{
    std::string name;          ///< the file name it was computed for
    MediaKind kind = MediaKind::Unknown;
    std::string format;        ///< "tap", "tzx", "trd", "scl", "fdi", "udi", "td0", "hobeta", "sna", "z80", "szx", ...
    size_t size = 0;           ///< file bytes
    std::string sha256;        ///< exact, lower-case hex
    std::string sha1, md5;     ///< Options::externalHashes only
    /// The content signature with its prefix (`tape:sha256:`, `trdset:sha256:`, `mem:sha256:`); empty when the
    /// media has none (an unknown file, a tape of pulses only, a disk without TR-DOS)
    std::string content;
    std::vector<TapeBlockHash> blocks;
    std::vector<DiskFile> files;
    std::vector<MemoryPage> pages;
    std::string machineHint;   ///< a snapshot's (snapshot::Image::machineHint)
    std::vector<std::string> warnings;

    /// `file:sha256:<hex>` and the content signature: what a game profile's `match.signatures` holds
    std::vector<std::string> Keys() const;
    /// The TR-DOS file set as match.h tokens, sorted; empty for other media
    std::vector<std::string> FileSet() const;
};

/// Signatures of a file in memory; @p name gives the format where the content has no mark (TRD by its extension).
/// False only when nothing could be read (an empty buffer); an unknown format still gets its exact hash
bool Compute(const uint8_t* data, size_t size, const std::string& name, Signatures& out, std::string& error,
             const Options& options = {});
/// The same for a file on disk
bool ComputeFile(const std::string& path, Signatures& out, std::string& error, const Options& options = {});

/// How two signatures relate, strongest first
enum class MatchLevel : uint8_t
{
    None,
    Exact,     ///< the same file
    Content,   ///< the same tape payloads / TR-DOS file set / snapshot RAM
    Fuzzy,     ///< file sets by match.h's rule
    Memory,    ///< snapshot RAM pages by kMemoryShare
};
const char* ToText(MatchLevel level);

struct MatchResult
{
    MatchLevel level = MatchLevel::None;
    double score = 0.0;        ///< 1 for exact / content, the Jaccard or the page share otherwise
    std::string reason;        ///< for people: "file set: Jaccard 0.83, 5 of 6 files"
};
/// @p reference (the known title) against @p candidate (what was loaded)
MatchResult Compare(const Signatures& reference, const Signatures& candidate);

/// The share of equal pages over the pages both snapshots hold (by bank and page); 0 below kMemoryMinPages common
double MemoryShare(const std::vector<MemoryPage>& a, const std::vector<MemoryPage>& b);

/// The tree every surface converts (JSON, py::dict, CLI text): kind, format, size, sha256 (sha1, md5), content, keys,
/// file_set and, with @p details, the blocks, files and the page count
StateNode ToStateNode(const Signatures& signatures, bool details);
/// A comparison as a tree: level, score, reason
StateNode ToStateNode(const MatchResult& result);

}  // namespace unrealng::swsignature
