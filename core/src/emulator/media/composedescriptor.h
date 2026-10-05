#pragma once

/// @file composedescriptor.h
/// @brief The composition descriptor (`*.ucompose.yaml` / `*.ucompose.json`):
/// what a composite medium is built from. Parsed with the same rules as the
/// folder manifest: every key optional unless marked, unknown keys and bad
/// values are reported and never fatal, a malformed file becomes a report.
///
/// ```yaml
/// version: 1
/// target: {fs: auto, size: 2GiB, free: 256MiB, label: GAMES, codepage: cp866, partition: mbr}
/// layers:                       # bottom first
///   - {name: base, source: {folder: ../sd}}
///   - {name: games, source: {folder: ~/zx/games}, mount: /GAMES, include: ["*.trd"], exclude: ["*.bak"]}
/// ```
/// Paths are relative to the descriptor's folder; `~` is the home folder.
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §1.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/media/mediatypes.h"

struct ComposeSource
{
    enum class Kind : uint8_t
    {
        Folder,
        Image,  ///< a FAT disk image (phase C3)
        Iso,    ///< an ISO 9660 image (phase C5)
    };
    Kind kind = Kind::Folder;
    std::filesystem::path path;          ///< absolute after normalization
    std::optional<uint32_t> partition;   ///< Image: 1-based MBR partition
    std::optional<CodePage> codePage;    ///< Image: short names of the source
};

enum class DeletePolicy : uint8_t
{
    Keep,    ///< the host file stays, a whiteout hides it (default)
    Trash,   ///< the host's trash
    Move,    ///< the deleted-files folder
    Delete,  ///< removed for good
    Ignore,  ///< not carried over: the file reappears on the next build
};

struct ComposeLayer
{
    std::string name;
    ComposeSource source;
    std::string mount = "/";
    std::string from = "/";
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::vector<std::string> opaque;
    std::vector<std::string> whiteout;
    ConflictPolicy conflict = ConflictPolicy::Shadow;
    bool writable = false;
    DeletePolicy onDelete = DeletePolicy::Keep;
    std::filesystem::path deletedFolder;
};

struct ComposeTarget
{
    enum class Fs : uint8_t
    {
        Auto,
        Fat16,
        Fat32,
        Iso9660,
    };
    enum class Build : uint8_t
    {
        Auto,
        Rebuild,
        Graft,
    };
    std::optional<MediaKind> kind;          ///< Block / Optical; unset: the slot's kind
    Fs fs = Fs::Auto;
    Build build = Build::Auto;
    std::optional<uint64_t> size;           ///< total bytes
    std::optional<uint64_t> free;           ///< room for guest writes
    std::optional<std::string> label;
    std::optional<CodePage> codePage;
    std::optional<bool> mbr;                ///< partition: mbr | none; unset: the slot's default
    std::optional<int64_t> fixedTimeUtc;    ///< reproducible builds: every timestamp this value
    std::string onBadName = "skip";         ///< skip | replace
};

struct ComposeWrites
{
    AccessMode access = AccessMode::Session;
    std::string save = "delta";             ///< flat | delta | commit | write-back (D-7)
    std::string upper;                      ///< S4 copy-up layer
    std::filesystem::path delta;            ///< S2 file
};

struct ComposeDescriptor
{
    int version = 0;
    ComposeTarget target;
    std::vector<ComposeLayer> layers;
    bool hasPartitions = false;             ///< partitions mode (phase C7)
    bool hasBoot = false;                   ///< boot layer (phases C5 / C8)
    ComposeWrites writes;

    std::filesystem::path file;             ///< empty for an inline descriptor
    std::filesystem::path baseDir;          ///< relative paths resolve against it
    std::vector<std::string> report;        ///< unknown keys, bad values
    std::string error;                      ///< fatal: no version, no layers, unreadable file

    bool Ok() const { return error.empty(); }

    /// Read and parse a descriptor file (YAML, or JSON for `.json`)
    static ComposeDescriptor Load(const std::filesystem::path& file);
    /// Parse text; `baseDir` resolves relative paths, `sourceName` names it in reports
    static ComposeDescriptor Parse(const std::string& text, const std::filesystem::path& baseDir,
                                   const std::string& sourceName);

    /// Canonical JSON of the normalized descriptor: defaults filled, paths
    /// absolute, keys in a fixed order. Formatting differences of the source
    /// never change it (the content id hashes it)
    std::string Normalized() const;

    /// "2GiB", "256MiB", "64KiB", "1.44MB", "1048576" -> bytes; false if malformed
    static bool ParseSize(const std::string& text, uint64_t& bytes);
    /// The file names that are descriptors: *.ucompose.yaml, *.ucompose.yml, *.ucompose.json
    static bool IsDescriptorName(const std::string& fileName);
};
