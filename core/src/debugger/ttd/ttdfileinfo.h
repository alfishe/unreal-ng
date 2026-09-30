#pragma once

/// @file ttdfileinfo.h
/// @brief What a .ttd session file holds and which machine it was recorded on,
/// read without loading the session.
///
/// A session only loads into a machine that matches the recording: the same
/// model, ROM set and devices in the device slots (the loader refuses a
/// General Sound or TurboSound card other than the recorded one). A caller
/// that wants to provision the right instance BEFORE the load - pick the
/// model, fit the recorded General Sound card - reads the file's info first.
///
/// Reading parses headers only:
///   - the fixed file header (model, ROM signature, frame range, counts,
///     section flags);
///   - the recorded device set: the header's peripheral mask (files written
///     since the mask exists, flag kFlagsHasPeripheralMask); older files are
///     walked to the first checkpoint instead - page slot headers are read and
///     their payloads skipped, nothing is decompressed.
///
/// The recorded machine of the CURRENT session (loaded or being recorded) is
/// TimeTravelManager::GetSessionInfo().machine - the same TTDRecordedMachine.

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

#include "emulator/platform.h"   // GSTypeKind, MEM_MODEL

namespace ttd
{

/// The machine a session was recorded on: what an instance must match to load it.
struct TTDRecordedMachine
{
    uint8_t modelId = 0;              ///< MEM_MODEL value
    std::string model;                ///< Short model name (Config::mem_model, e.g. "PENTAGON"); "" if unknown
    uint16_t ramPageBound = 0;        ///< Exclusive RAM page-index bound of the model
    uint64_t romSignature = 0;        ///< FNV-1a of the ROM region; 0 = unknown (not checked)

    /// Bit i set: peripheral id i (PeripheralId) was fitted - its state is in
    /// every checkpoint. The set the loader checks the live machine against.
    uint64_t peripheralMask = 0;
    std::vector<std::string> peripherals;  ///< Names of the fitted peripherals, ascending id

    /// General Sound slot: NONE, Z80 (GS with its Z80), LW (lightweight), NGS (NeoGS)
    GSTypeKind generalSound = GSTypeKind::NONE;
    /// TurboSound slot device: "none", "turbosound" (2 x AY) or "tsfm" (2 x YM2203)
    std::string turboSound = "none";
};

/// A .ttd file's header, sections and recorded machine.
struct TTDFileInfo
{
    std::string path;                 ///< Source path (when read from a path)
    uint64_t fileBytes = 0;           ///< File size (when read from a path)

    uint16_t schemaVersion = 0;
    uint16_t flags = 0;               ///< Raw header flags (ttddumpformat.h)
    uint16_t cpuStateSize = 0;        ///< Producer's struct sizes (a build with other sizes refuses the file)
    uint16_t chipsetStateSize = 0;
    uint64_t capturedAtUnixMs = 0;    ///< Wall-clock capture time
    std::string emulatorId;           ///< Symbolic id of the recording instance
    uint8_t sessionState = 0;         ///< TTDSessionState at capture (0 idle, 1 recording, 2 detached)
    uint64_t startFrame = 0;          ///< First / last captured frame
    uint64_t endFrame = 0;
    uint32_t pageStoreCount = 0;      ///< Live page slots
    uint32_t checkpointCount = 0;

    // Sections (header flags, decoded)
    bool hasWriteJournal = false;
    bool writeJournalComplete = false;
    bool hasCoverageIndex = false;
    bool hasBookmarks = false;
    bool hasInputJournal = false;
    bool hasExternalEvents = false;
    bool hasPortJournals = false;
    bool topClockTime = false;

    /// true: the device set came from the header's mask; false: found by
    /// walking to the first checkpoint (a file written before the mask)
    bool peripheralsFromHeader = false;

    TTDRecordedMachine machine;
};

/// Name of a peripheral id ("betadisk", "gs", "neogs", ...); "id<N>" for an id
/// this build does not know.
std::string PeripheralIdName(uint8_t id);

/// The General Sound personality a fitted-peripheral mask records (NONE when
/// no General Sound card was fitted).
GSTypeKind GeneralSoundOf(uint64_t peripheralMask);

/// Short name of a GSTypeKind as used on the automation surfaces: "none",
/// "z80", "lw", "ngs" ("bass" for the unused BASS kind).
const char* GeneralSoundName(GSTypeKind kind);

/// Fill the derived fields of `machine` (model name, peripheral names, General
/// Sound kind, TurboSound device) from modelId and peripheralMask.
void DescribeRecordedMachine(TTDRecordedMachine& machine);

/// Read a .ttd file's info from a stream positioned at the file start.
/// Headers only (see the file comment). The stream is left at an unspecified
/// position. @return false with `err` set on a stream that is not a readable
/// .ttd file (bad magic, unsupported schema, truncated header).
bool ReadTTDFileInfo(std::istream& in, TTDFileInfo& info, std::string& err);

/// Same, from a UTF-8 path; also fills `path` and `fileBytes`.
bool ReadTTDFileInfo(const std::string& path, TTDFileInfo& info, std::string& err);

}  // namespace ttd
