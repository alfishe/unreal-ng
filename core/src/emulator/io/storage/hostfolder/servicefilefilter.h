#pragma once

/// @file servicefilefilter.h
/// @brief Host service files that never become part of a medium built from a
/// folder: .DS_Store, Thumbs.db, .git ... The rules are data, grouped in named
/// collections, never ad-hoc checks in the builders. Every collection applies
/// on every host, because a folder copied from a Mac to Windows still carries
/// its .DS_Store. Design: docs/inprogress/2026-09-28-storage-manager/
/// technical-design.md §6.0.

#include <string>
#include <vector>

/// Case-insensitive wildcard match: `*` any run (also empty), `?` one character
bool MatchesWildcard(const std::string& pattern, const std::string& name);

class ServiceFileFilter
{
public:
    struct Collection
    {
        std::string name;                   ///< "macos", "windows", ...
        std::vector<std::string> patterns;  ///< wildcards, matched case-insensitively
    };

    /// The built-in collections: macos, windows, linux, vcs, unreal
    ServiceFileFilter();

    /// True when `name` (a file or folder name, no path) is a service entry;
    /// `collection` receives the name of the collection that matched
    bool IsService(const std::string& name, std::string* collection = nullptr) const;

    void AddCollection(Collection collection) { _collections.push_back(std::move(collection)); }
    const std::vector<Collection>& Collections() const { return _collections; }

    static const std::vector<Collection>& DefaultCollections();

private:
    std::vector<Collection> _collections;
};
