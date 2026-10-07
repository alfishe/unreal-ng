#pragma once

/// @file fatimagesource.h
/// @brief A FAT disk image as a FileTree: one layer of a composite medium.
/// The image's files stay where they are: each becomes extents of the image
/// device (FatVolumeReader::ChainExtents), read through ExtentReader. The
/// image is only read. DT-1 applies as for folders: `from`, exclude patterns,
/// include patterns (files only); a file whose cluster chain is broken is left out with a
/// report line. Design: docs/inprogress/2026-10-05-media-multisource/phases/c3-image-sources.md.
///
/// C4b (phases/c4b-lazy-graft-base.md): with `lazy` only the root directory is read; every
/// subdirectory is an `unexpanded` node that FatImageExpander reads when a build needs it.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/fat/fatvolumereader.h"

class SourcePool;

struct FatImageSourceOptions
{
    std::string from = "/";             ///< directory of the image that becomes the layer's root
    std::vector<std::string> include;   ///< wildcards on file names; empty: every file
    std::vector<std::string> exclude;   ///< wildcards on names, files and directories alike
    std::optional<uint32_t> partition;  ///< MBR entry 1-4; none: a superfloppy or the first FAT partition
    CodePage codePage = CodePage::Cp866;  ///< the image's short names
    bool lazy = false;                  ///< C4b: subdirectories stay unexpanded
};

class FatImageSource
{
public:
    /// Build the layer's tree from the image registered in `pool` as `device`.
    /// A partition is registered as a SubRangeDevice of it (once per image and
    /// partition). `identity` gets the source's identity (the volume device's
    /// content id and the code page). False with `error` when the image has no
    /// such FAT volume, `from` is not a directory, or a directory cannot be read.
    /// `volume` gets the pool device the tree's extents name (the image, or its partition window)
    static bool Enumerate(uint16_t device, const FatImageSourceOptions& options, SourcePool& pool, FileTree& out,
                          std::vector<std::string>* report, std::string* error, uint64_t* identity = nullptr,
                          uint16_t* volume = nullptr);
};

/// C4b: reads the unexpanded directories of a tree FatImageSource enumerated lazily, from the same volume
/// with the same options (filters, code page)
class FatImageExpander
{
public:
    /// `volume`: what Enumerate returned in its `volume`
    FatImageExpander(SourcePool& pool, uint16_t volume, const FatImageSourceOptions& options);

    /// Read the entries of unexpanded directory `node` (its subdirectories stay unexpanded)
    bool Expand(FileTree& tree, uint32_t node, std::vector<std::string>* report, std::string* error);
    /// Expand every directory on `path` ("/GAMES/SUB", components matched by the FAT key), as far as the tree
    /// has directories there
    bool ExpandPath(FileTree& tree, const std::string& path, std::vector<std::string>* report, std::string* error);
    /// Read everything still unexpanded: the tree Enumerate without `lazy` builds
    bool ExpandAll(FileTree& tree, std::vector<std::string>* report, std::string* error);

    /// The files and their bytes under the directory at `cluster`, from directory listings (no FAT chains;
    /// the filters apply as for the tree). Remembered per cluster
    std::pair<uint64_t, uint64_t> Count(uint32_t cluster);

    /// Directories read so far by Expand, ExpandPath and ExpandAll (tests)
    uint32_t DirectoriesRead() const { return _directoriesRead; }

private:
    void CountInto(uint32_t cluster, int depth, uint64_t& files, uint64_t& bytes);

    SourcePool& _pool;
    uint16_t _volume;
    FatImageSourceOptions _options;
    FatVolumeReader _reader;
    bool _open = false;
    uint32_t _directoriesRead = 0;
    std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> _counts;
};
