#pragma once

/// @file mediaformatregistry.h
/// @brief The one place that knows formats (technical design §4): probes a
/// source for a slot kind and builds the medium, access layer included.
/// Block media: raw images, HDF / HDI / fixed VHD (IDE), and host folders as
/// FAT16 / FAT32 volumes (M1). Optical media: ISO 9660 images (CD drives).
/// Floppies: every disk image format (FloppyFormats), and host folders built
/// into a TR-DOS disk (M2). Tapes: every TapeLoaderRegistry format, and host
/// folders built into a TZX (M3).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;

struct OpenRequest
{
    EmulatorContext* context = nullptr;  ///< the machine's settings for format loaders (TR-DOS interleave)
    MediaSource source;
    MediaKind kind = MediaKind::Block;
    AccessMode access = AccessMode::Session;
    FatType fs = FatType::Fat16;
    /// The FAT flavours the slot's controller reads (SlotDescriptor::
    /// fsCompatibility). Empty: no constraint. A folder that does not fit a
    /// FAT16 volume switches to FAT32 when FAT32 is among them or the list is
    /// empty (BUGS.md #2)
    std::vector<FatType> allowedFs;
    std::optional<CodePage> codePage;  ///< folder volumes: explicit > the folder's manifest > CP866
    std::optional<uint64_t> freeBytes; ///< folder volumes: room for guest writes (default 256 MiB)

    /// Folder volumes only (BUGS.md #3): forwarded to FolderScanOptions /
    /// FolderDiskBuilder::BuildTrd so a caller scanning off the UI thread can
    /// abort a large or slow/network folder and report progress. Empty: no
    /// cancellation, no progress (a plain image source never looks at these)
    std::function<bool()> cancelRequested;
    std::function<void(uint64_t entriesScanned, uint64_t bytesScanned)> onProgress;
};

class MediaFormatRegistry
{
public:
    /// Build the medium for `request`. On failure `medium` stays empty and the
    /// result says why
    static MediaResult Open(const OpenRequest& request, std::unique_ptr<Medium>& medium);

    /// Wrap a ready block device (tests, builders) in the access layer
    static std::unique_ptr<Medium> WrapBlock(MediaSource source, AccessMode access, std::string format,
                                             std::unique_ptr<IBlockDevice> base, MediaKind kind = MediaKind::Block);

    /// File extensions offered for a kind (GUI filters, MCP descriptions, errors)
    static std::vector<std::string> Extensions(MediaKind kind);
};
