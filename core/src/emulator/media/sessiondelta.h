#pragma once

/// @file sessiondelta.h
/// @brief S2: a composite's change layer kept in a file next to its
/// descriptor, so the guest's writes survive an eject or a restart while the
/// sources stay untouched. Restored on insert only over the same composite
/// (content id and size), since a sector delta means something only over
/// byte-identical sources.
///
/// File layout (little endian):
///   "UNGDELTA", u32 version (1), u32 flags (0), u64 content id, u64 sector
///   count, u64 changed sectors, u32 run count, u32 layer count;
///   per layer: u64 identity, u16 name length, the name (UTF-8);
///   per run: u64 lba, u32 sectors;
///   u32 chunk count; per chunk: u32 raw bytes, u32 stored bytes, u8 codec
///   (0 stored, 1 zstd), 3 zero bytes, the payload (the runs' sectors in
///   order, 1 MiB raw per chunk);
///   "UNGDEND!", u64 FNV-1a of the raw sector data.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c6-provenance-flatten.md §4,
/// flatten-strategies.md S2, DT-13.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "emulator/media/mediatypes.h"

class SessionWriteMap;

/// What a delta was written over: the composite and each layer's source identity
struct DeltaIdentity
{
    struct Layer
    {
        std::string name;
        uint64_t identity = 0;
    };
    uint64_t contentId = 0;
    uint64_t sectorCount = 0;
    std::vector<Layer> layers;
};

enum class DeltaLoad : uint8_t
{
    Missing,   ///< no file: the change layer stays empty
    Restored,  ///< loaded into the change layer
    Damaged,   ///< unreadable, truncated or of another size: not applied
    Mismatch,  ///< written over other sources: not applied (the detail names the layers)
};

class SessionDelta
{
public:
    static constexpr uint32_t kVersion = 1;

    /// Write the change layer of `map` to `path` (a temp file, then renamed into place)
    static MediaResult Save(const std::filesystem::path& path, const SessionWriteMap& map, const DeltaIdentity& identity);

    /// Load `path` into `map` (emptied first) when it was written over `identity`. `detail` says why
    /// not, or how many sectors were restored
    static DeltaLoad Load(const std::filesystem::path& path, SessionWriteMap& map, const DeltaIdentity& identity, std::string& detail);
};
