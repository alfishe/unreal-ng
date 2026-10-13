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

#include "debugger/ttd/ttdserializable.h"   // TTDDeviceKey, PeripheralId
#include "emulator/platform.h"   // GSTypeKind, MEM_MODEL
#include "ttdperipheralmask.h"

namespace ttd
{

/// A device a session recorded (or names as fitted but not recorded)
struct TTDRecordedDevice
{
    TTDDeviceKey key;
    PeripheralId legacyId = PeripheralId::None;   ///< its v1 id, None when it has none (or the file does not say)
};

/// The machine a session was recorded on: what an instance must match to load it.
struct TTDRecordedMachine
{
    uint8_t modelId = 0;              ///< MEM_MODEL value
    std::string model;                ///< Short model name (Config::mem_model, e.g. "PENTAGON"); "" if unknown
    uint16_t ramPageBound = 0;        ///< Exclusive RAM page-index bound of the model
    uint64_t romSignature = 0;        ///< FNV-1a of the ROM region; 0 = unknown (not checked)

    /// The fitted devices: their state is in every checkpoint. The set the
    /// loader checks the live machine against (an engine file's device table,
    /// a v1 file's blob ids)
    std::vector<TTDRecordedDevice> devices;
    std::vector<std::string> peripherals;  ///< Their names (DeviceDisplayName), ascending v1 id, then type, instance
    /// Devices fitted but not recorded by design (the lightweight General Sound)
    std::vector<TTDRecordedDevice> notRecordedDevices;
    std::vector<std::string> notRecorded;  ///< Their names

    /// A device of @p type is fitted (recorded or not)
    bool Fits(TTDDeviceType type) const;
    /// The recorded devices that have a v1 id, as the set of those ids (what a v1 file's header names; the
    /// `peripheral_mask` of the automation surfaces). A device without a v1 id is not in it
    PeripheralMask V1Mask() const;

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

    /// v1 file: true when the device set came from the header's mask; false:
    /// found by walking to the first checkpoint (a file written before the mask)
    bool peripheralsFromHeader = false;

    TTDRecordedMachine machine;
};

/// Name of a v1 peripheral id ("betadisk", "gs", "neogs", ...); "id<N>" for an
/// id this build does not know.
std::string PeripheralIdName(uint8_t id);

/// The name automation shows for a recorded device: its v1 id's name (the names
/// sessions always had), a device of a pre-freeze type without one by its
/// type's, any other by its instance
std::string DeviceDisplayName(const TTDRecordedDevice& device);

/// The devices of a v1 file's mask: id i set = v1 id i (type i, its name as instance)
std::vector<TTDRecordedDevice> DevicesOfV1Mask(const PeripheralMask& mask);

/// The General Sound personality a recorded machine has fitted (NONE when no
/// General Sound card was fitted).
GSTypeKind GeneralSoundOf(const TTDRecordedMachine& machine);

/// Short name of a GSTypeKind as used on the automation surfaces: "none",
/// "z80", "lw", "ngs" ("bass" for the unused BASS kind).
const char* GeneralSoundName(GSTypeKind kind);

/// Fill the derived fields of `machine` (model name, peripheral names, General
/// Sound kind, TurboSound device) from modelId and its devices.
void DescribeRecordedMachine(TTDRecordedMachine& machine);

/// Read a .ttd file's info from a stream positioned at the file start.
/// Headers only (see the file comment). The stream is left at an unspecified
/// position. @return false with `err` set on a stream that is not a readable
/// .ttd file (bad magic, unsupported schema, truncated header).
bool ReadTTDFileInfo(std::istream& in, TTDFileInfo& info, std::string& err);

/// Same, from a UTF-8 path; also fills `path` and `fileBytes`.
bool ReadTTDFileInfo(const std::string& path, TTDFileInfo& info, std::string& err);

}  // namespace ttd
