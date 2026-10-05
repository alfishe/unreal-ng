#pragma once

/// @file unionbuilder.h
/// @brief Merge the layers of a composite medium into one tree, once, at build
/// time (multi-source decision tree DT-2): upper layers shadow lower ones by
/// path under the target's name equivalence, directories merge, whiteouts
/// remove, opaque directories hide what is below. After the merge there are
/// no layers left at read time, only a tree whose files point into sources.
///
/// Entries of the same layer are never merged with each other, even when the
/// target folds their names together: a case-sensitive host folder holding
/// both "a.txt" and "A.TXT" keeps both, as a one-folder volume always did.
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §3.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/storage/compose/filetree.h"

enum class ConflictPolicy : uint8_t
{
    Shadow,     ///< the upper entry wins (default)
    KeepLower,  ///< the lower entry wins
    Error,      ///< any shadowing fails the build
};

struct UnionLayer
{
    const FileTree* tree = nullptr;
    std::string name;                    ///< for reports
    std::string mount = "/";             ///< where the layer's root lands in the target
    ConflictPolicy conflict = ConflictPolicy::Shadow;
    std::vector<std::string> opaque;     ///< target paths: the layer's directory hides the lower one's contents
    std::vector<std::string> whiteout;   ///< target paths removed before the layer merges
};

class UnionBuilder
{
public:
    /// The name key two entries share when the target treats them as one entry
    using KeyFunction = std::string (*)(const std::string& name);

    /// FAT: case-insensitive (the letters the short-name code pages know),
    /// trailing dots and spaces ignored, as FAT matches long names
    static std::string FatKey(const std::string& name);
    /// ISO 9660 with Joliet: the name itself (Joliet keeps both "a.txt" and "A.TXT")
    static std::string ExactKey(const std::string& name) { return name; }

    /// Merge `layers` (bottom first) into `out` (a fresh tree). Every
    /// shadowing, whiteout and opaque directory is listed in `report`. False
    /// with `error` when a layer with ConflictPolicy::Error would shadow an entry
    static bool Merge(const std::vector<UnionLayer>& layers, KeyFunction key, FileTree& out,
                      std::vector<std::string>* report, std::string* error);

    /// Directories first, then files, each byte-wise by name: the order of a
    /// folder snapshot, and of every merged directory
    static void SortChildren(FileTree& tree, uint32_t dir);
};
