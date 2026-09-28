#pragma once

/// @file mediaformatregistry.h
/// @brief The one place that knows formats (technical design §4): probes a
/// source for a slot kind and builds the medium, access layer included.
/// M1 serves block media: raw images, and host folders as FAT16 / FAT32
/// volumes; floppy and tape formats join in M2 / M3.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

struct OpenRequest
{
    MediaSource source;
    MediaKind kind = MediaKind::Block;
    AccessMode access = AccessMode::Session;
    FatType fs = FatType::Fat16;
    std::optional<CodePage> codePage;  ///< folder volumes: explicit > the folder's manifest > CP866
    std::optional<uint64_t> freeBytes; ///< folder volumes: room for guest writes (default 256 MiB)
};

class MediaFormatRegistry
{
public:
    /// Build the medium for `request`. On failure `medium` stays empty and the
    /// result says why
    static MediaResult Open(const OpenRequest& request, std::unique_ptr<Medium>& medium);

    /// Wrap a ready block device (tests, builders) in the access layer
    static std::unique_ptr<Medium> WrapBlock(MediaSource source, AccessMode access, std::string format,
                                             std::unique_ptr<IBlockDevice> base);

    /// File extensions offered for a kind (GUI filters, MCP descriptions, errors)
    static std::vector<std::string> Extensions(MediaKind kind);
};
