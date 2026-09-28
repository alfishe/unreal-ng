#pragma once

/// @file mediaformatregistry.h
/// @brief The one place that knows formats (technical design §4): probes a
/// source for a slot kind and builds the medium, access layer included.
/// M1 serves block media (raw images; host folders arrive with the folder
/// pipeline); floppy and tape formats join in M2 / M3.

#include <memory>
#include <string>
#include <vector>

#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

struct OpenRequest
{
    MediaSource source;
    MediaKind kind = MediaKind::Block;
    AccessMode access = AccessMode::Session;
    FatType fs = FatType::Fat16;
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
