#pragma once

/// @file isoimagesource.h
/// @brief An ISO 9660 volume (the first data track of a CD image) as a
/// FileTree: one layer of a composite medium, block or optical. Files stay in
/// the image: each section of a file is one extent of the CdImage device
/// (ISO block b = sectors 4b..4b+3). Names: Joliet when the volume has it,
/// else the ISO names without their ";1". DT-1 applies as for the other
/// sources: `from`, exclude (names), include (file names).
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c5-iso.md.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/storage/compose/filetree.h"

class SourcePool;

struct IsoImageSourceOptions
{
    std::string from = "/";
    std::vector<std::string> include;   ///< wildcards on file names; empty: every file
    std::vector<std::string> exclude;   ///< wildcards on names, files and directories alike
};

class IsoImageSource
{
public:
    /// Build the layer's tree from the CD image registered in `pool` as `device`.
    /// `identity` gets the image's content id. False with `error` when the
    /// image holds no ISO 9660 volume or `from` is not a directory of it
    static bool Enumerate(uint16_t device, const IsoImageSourceOptions& options, SourcePool& pool, FileTree& out,
                          std::vector<std::string>* report, std::string* error, uint64_t* identity = nullptr);
};
