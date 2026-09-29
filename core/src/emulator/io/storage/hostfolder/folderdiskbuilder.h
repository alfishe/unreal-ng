#pragma once

/// @file folderdiskbuilder.h
/// @brief A host folder built into a TR-DOS floppy image, once, in memory.
///
/// The build is one-way: the files are read when the disk is inserted, the
/// guest then reads and writes the in-memory image, and saving is the usual
/// "save the disk image" request. Nothing is ever written back into the folder.
///
/// Rules (integration-floppy.md §4):
///   - one folder, top level only; subfolders are skipped and reported
///   - the manifest's `order` first, then the rest byte-wise sorted
///   - files are placed in that order; one that does not fit (catalog full,
///     no room, over 255 sectors) is skipped and reported, and placing goes on
///   - Hobeta files (`*.$B`, `*.$C`, ... with a valid header) keep their
///     header's name, type, start and length
///   - other files: the name becomes 8 printable ASCII characters, the type
///     comes from DiskTypeMap, code loads at 32768 (a 6912-byte screen at
///     16384); the manifest's `files:` overrides name, type, start and the
///     BASIC autorun line
///   - a name and type already on the disk: the last character becomes 1...9
///   - the label: the manifest's, else the folder name
///
/// Worked example: `boot.$B`, `game.$C`, `intro.scr` (6 912 bytes) and a
/// manifest `order: [boot.$B, intro.scr]` give the catalog
/// `boot    B`, `intro   C` (start 16384, 27 sectors), `game    C`.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;
struct FolderEntry;
struct FolderManifest;

/// File extension -> TR-DOS file type. Data in the class, like the service
/// file rules: the builders never test extensions ad hoc
class DiskTypeMap
{
public:
    struct Rule
    {
        const char* extension;  ///< lower case, no dot
        char type;
    };

    /// The TR-DOS type for a host file's extension (lower or upper case, no
    /// dot); 'C' when no rule names it
    static char TypeFor(const std::string& extension);

    static const std::vector<Rule>& Rules();
};

class FolderDiskBuilder
{
public:
    /// The TR-DOS name for a host name: printable ASCII kept (case too), '"',
    /// control and non-ASCII characters (one per code point) become '_',
    /// then cut or space-padded to `width` bytes (8 on TR-DOS, 10 on tape)
    static std::string CompatibleName(const std::string& utf8Name, size_t width = 8);

    /// The top-level files of `root` in medium order: the manifest's `order`
    /// first, then the rest as scanned (byte-wise sorted). Names in `order`
    /// that are not in the folder are reported
    static std::vector<const FolderEntry*> OrderFiles(const FolderEntry& root, const FolderManifest& manifest,
                                                      std::vector<std::string>& report);

    /// Build a TR-DOS disk from `folder`. `context` provides the machine's
    /// TR-DOS format settings (sector interleave). The result's report lists
    /// every file left out and why
    static MediaResult BuildTrd(EmulatorContext* context, const std::filesystem::path& folder,
                                std::unique_ptr<DiskImage>& disk);
};
