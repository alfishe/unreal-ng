#pragma once

/// @file fatimagesource.h
/// @brief A FAT disk image as a FileTree: one layer of a composite medium.
/// The image's files stay where they are: each becomes extents of the image
/// device (FatVolumeReader::ChainExtents), read through ExtentReader. The
/// image is only read. DT-1 applies as for folders: `from`, exclude patterns,
/// include patterns (files only); a file whose cluster chain is broken is left out with a
/// report line. Design: docs/inprogress/2026-10-05-media-multisource/phases/c3-image-sources.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/filetree.h"

class SourcePool;

struct FatImageSourceOptions
{
    std::string from = "/";             ///< directory of the image that becomes the layer's root
    std::vector<std::string> include;   ///< wildcards on file names; empty: every file
    std::vector<std::string> exclude;   ///< wildcards on names, files and directories alike
    std::optional<uint32_t> partition;  ///< MBR entry 1-4; none: a superfloppy or the first FAT partition
    CodePage codePage = CodePage::Cp866;  ///< the image's short names
};

class FatImageSource
{
public:
    /// Build the layer's tree from the image registered in `pool` as `device`.
    /// A partition is registered as a SubRangeDevice of it (once per image and
    /// partition). `identity` gets the source's identity (the volume device's
    /// content id and the code page). False with `error` when the image has no
    /// such FAT volume, `from` is not a directory, or a directory cannot be read
    static bool Enumerate(uint16_t device, const FatImageSourceOptions& options, SourcePool& pool, FileTree& out,
                          std::vector<std::string>* report, std::string* error, uint64_t* identity = nullptr);
};
