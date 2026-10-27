#pragma once

/// @file hosttrash.h
/// @brief A host file or folder moved to the host's trash, where the user can get it back
/// (multi-source phases/c8d-writeback-tails.md §2):
/// - Windows: the Recycle Bin (`SHFileOperationW`, `FOF_ALLOWUNDO`);
/// - macOS: `~/.Trash` (a unique name when taken; Finder shows it, "Put Back" is not offered);
/// - Linux and the BSDs: the freedesktop.org Trash specification 1.0 (`$XDG_DATA_HOME/Trash`, or
///   `$topdir/.Trash-$uid` for a file on another device), with its `.trashinfo`.

#include <filesystem>
#include <string>

namespace HostTrash
{
    /// Move `path` to the trash. False with `error` when the host has no usable trash for it (nothing moved)
    bool Move(const std::filesystem::path& path, std::string* error = nullptr);

    /// freedesktop.org: the `.trashinfo` text of `original` deleted at local time `deletionDate` (YYYY-MM-DDThh:mm:ss)
    std::string TrashInfo(const std::filesystem::path& original, const std::string& deletionDate);

    /// freedesktop.org: the trash directory for a file on another device than the home: `$topdir/.Trash-$uid`
    std::filesystem::path TopDirTrash(const std::filesystem::path& topdir, unsigned uid);
}  // namespace HostTrash
