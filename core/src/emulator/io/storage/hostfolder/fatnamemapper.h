#pragma once

/// @file fatnamemapper.h
/// @brief Host names -> FAT directory names, and back.
///
/// Every file gets an 8.3 short name in an 8-bit code page (CP866 by default,
/// the OEM code page of NedoOS and other Spectrum FAT software, or CP1251)
/// and, when the short name cannot say it all, a long name (LFN) in UTF-16.
/// The rules follow Windows:
///   - upper case; Cyrillic letters are code-page bytes; spaces and extra dots
///     are dropped; `+ , ; = [ ]` and anything the page cannot hold become `_`;
///   - base cut to 8, extension (after the last dot) to 3;
///   - a name that lost anything gets a numeric tail `~1`, `~2` ... rebuilt
///     from the base each time (never stacked, unlike xpeccy-plus);
///   - a long name is added when the short name lost anything or the case
///     differs from the host name.
/// Worked example (CP866): "Длинное имя.txt" -> "ДЛИННО~1TXT" (bytes #84 #8B
/// #88 #8D #8D #8E '~' '1' 'T' 'X' 'T') + LFN "Длинное имя.txt". In CP1251
/// the same letters are #C4 #CB #C8 #CD #CD #CE.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §6.3.

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "common/unicodehelper.h"

using FatShortName = std::array<uint8_t, 11>;  ///< 8 + 3 bytes, space padded, as stored in a directory entry

struct FatMappedName
{
    bool ok = false;
    std::string reason;          ///< why the name cannot be stored (ok == false)
    FatShortName shortName{};
    bool hasLongName = false;
    std::u16string longName;     ///< UTF-16, only when hasLongName
};

class FatNameMapper
{
public:
    /// Map the names of one folder, in order. Short names are unique within
    /// the folder (the volume label and "." / ".." are not in this list) and
    /// differ from every name in `taken` (entries already in the directory:
    /// a graft adds to a base directory)
    static std::vector<FatMappedName> MapFolder(const std::vector<std::string>& utf8Names, CodePage page = CodePage::Cp866,
                                                const std::set<FatShortName>& taken = {});

    /// The LFN checksum of a short name (the "sum" byte of every LFN entry)
    static uint8_t Checksum(const FatShortName& shortName);

    /// The LFN directory entries for `longName`, in on-disk order (the entry
    /// with the highest sequence number and the "last" flag first)
    static std::vector<std::array<uint8_t, 32>> LongNameEntries(const std::u16string& longName, uint8_t checksum);

    /// Reverse direction, for reading a volume: "NAME.EXT" from a short name
    /// (code page -> UTF-8; a leading #05 is the byte #E5)
    static std::string ShortNameToUtf8(const FatShortName& shortName, CodePage page = CodePage::Cp866);

    /// A short name from "NAME.EXT" text (volume labels, tests); no tail logic
    static FatShortName ShortNameFromText(const std::string& utf8, bool isLabel = false, CodePage page = CodePage::Cp866);
};
