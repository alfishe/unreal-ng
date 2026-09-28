#pragma once

/// @file foldermanifest.h
/// @brief The optional metafile of a folder source: `.unreal-media.yaml` or
/// `.unreal-media.json` in the folder itself. It sets order, names, types,
/// start addresses, excludes, the label, disk geometry and tape pauses. Every
/// key is optional; unknown keys and bad values are reported, never fatal.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §6.6.
///
/// ```yaml
/// label: MY GAMES
/// order: [boot.$B, game.$C]
/// exclude: [notes.txt, "*.psd"]
/// files:
///   loader.bin: {name: loader, type: C, start: 24576}
/// disk: {format: trd, tracks: 80, sides: 2}
/// tape: {format: tzx, pause: 1000}
/// ```

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct ManifestFileOverride
{
    std::optional<std::string> name;   ///< the name on the medium
    std::optional<char> type;          ///< TR-DOS type letter: B, C, D, #
    std::optional<uint16_t> start;     ///< start address (code) / autorun line (BASIC via `line`)
    std::optional<uint16_t> line;      ///< BASIC autorun line
};

struct FolderManifest
{
    std::optional<std::string> label;
    std::vector<std::string> order;                       ///< these names first, in this order
    std::vector<std::string> exclude;                     ///< wildcards on names
    std::map<std::string, ManifestFileOverride> files;    ///< keyed by host file name
    std::optional<std::string> diskFormat;                ///< "trd", ...
    std::optional<uint8_t> diskTracks;
    std::optional<uint8_t> diskSides;
    std::optional<std::string> tapeFormat;                ///< "tzx" | "tap"
    std::optional<uint32_t> tapePauseMs;

    std::string sourceFile;             ///< the metafile read, empty when none
    std::vector<std::string> report;    ///< unknown keys, bad values, both files present

    /// Read the metafile of `folder`, if any. A missing metafile is not an
    /// error; an unreadable or malformed one is reported and yields no settings
    static FolderManifest Load(const std::filesystem::path& folder);

    /// Parse YAML or JSON text (tests, and Load)
    static FolderManifest Parse(const std::string& text, const std::string& sourceName);

    static constexpr const char* kYamlName = ".unreal-media.yaml";
    static constexpr const char* kJsonName = ".unreal-media.json";
};
