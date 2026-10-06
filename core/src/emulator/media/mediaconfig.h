#pragma once

/// @file mediaconfig.h
/// @brief The media set of a machine as the config file states it.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §7.
///
/// ```ini
/// [MEDIA]
/// fdd.a           = games/elite.trd
/// sd.zc           = ~/zx/sdcard/       ; a folder -> FAT volume
/// sd.zc.access    = session            ; readonly | session | writethrough
/// sd.zc.fs        = fat16              ; folder volumes: fat16 | fat32
/// sd.zc.codepage  = cp866              ; folder volumes' short names: cp866 | cp1251
/// sd.zc.free      = 268435456          ; folder volumes: bytes of room for guest writes
/// sd.zc.wp        = 0                  ; the slot's write-protect switch
/// sd.zc.swapdelay = 500                ; ms the slot stays empty on a swap
/// SessionMemoryLimit = 128             ; MiB of session writes kept in memory, the rest spilled (0: no limit)
/// SpillFolder     = /var/tmp           ; where session spill files go (default: the system temp folder)
/// ```
///
/// SessionMemoryLimit and SpillFolder are settings, not slots (multi-source phases/c10d-session-spill.md).
///
/// A key is a slot id, or a slot id plus one option suffix. Slot ids contain
/// dots themselves (sd.zc, ide0.master), so the suffix is only split off when
/// it is one of the options above.
///
/// Legacy keys fill slots [MEDIA] leaves unset: [ZC] SDCardImage / SDCARD,
/// SDWrite, SDWriteProtect -> sd.zc; [NGS] SDCardImage / SDCARD -> sd.ngs;
/// [HDD] Image0 / Image1, HD0RO / HD1RO -> ide0.master / ide0.slave. They are
/// marked legacy: a machine without that slot ignores them quietly (the
/// shipped configs carry "[ZC] SDCARD=wc.img" on every model).
///
/// Paths: "~" is the home folder; a relative path is relative to the folder
/// of the config file.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

class IniFile;

struct MediaSetEntry
{
    std::string slotId;
    MediaSource source;
    std::optional<AccessMode> access;
    std::optional<FatType> fs;
    std::optional<CodePage> codePage;
    std::optional<uint64_t> freeBytes;
    std::optional<bool> writeProtect;
    std::optional<uint32_t> swapDelayMs;
    bool legacy = false;  ///< from a legacy section: quiet when the machine has no such slot
};

/// The [MEDIA] keys that are settings rather than slots
struct MediaSettings
{
    std::optional<uint64_t> sessionMemoryLimit;  ///< bytes
    std::optional<std::string> spillFolder;
};

class MediaConfig
{
public:
    /// The media set stated by `ini`. `configFolder` resolves relative paths;
    /// `report` receives unknown options and bad values
    static std::vector<MediaSetEntry> FromIni(const IniFile& ini, const std::string& configFolder,
                                              std::vector<std::string>* report = nullptr);

    /// SessionMemoryLimit (MiB) and SpillFolder; `report` receives bad values
    static MediaSettings SettingsFromIni(const IniFile& ini, const std::string& configFolder, std::vector<std::string>* report = nullptr);

    /// "~" expanded; a relative path joined to `configFolder`; normalized
    static std::string ResolvePath(const std::string& path, const std::string& configFolder);
};
