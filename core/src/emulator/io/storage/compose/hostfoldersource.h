#pragma once

/// @file hostfoldersource.h
/// @brief A scanned host folder as a FileTree: one layer of a composite
/// medium. File data stays in the host files (registered in the SourcePool);
/// nothing is read here. Decision tree DT-1 of the multi-source design
/// (entry admission) is applied: `from`, include patterns (files only), and
/// the hidden attribute for dot-names. Exclusions, service files, links and
/// size limits were already applied by FolderSnapshot::Scan.
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §2.

#include <string>
#include <vector>

#include "emulator/io/storage/compose/filetree.h"

class FolderSnapshot;
struct FolderEntry;
class SourcePool;

struct HostFolderSourceOptions
{
    std::string from = "/";             ///< subfolder of the snapshot that becomes the layer's root
    std::vector<std::string> include;   ///< wildcards on file names; empty: every file
};

class HostFolderSource
{
public:
    /// Build the layer's tree from `snapshot`. False with `error` when `from`
    /// does not exist or is not a folder. Files left out by `include` are
    /// listed in `report`
    static bool Enumerate(const FolderSnapshot& snapshot, const HostFolderSourceOptions& options, SourcePool& pool,
                          FileTree& out, std::vector<std::string>* report, std::string* error);
};
