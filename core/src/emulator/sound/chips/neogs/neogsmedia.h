#pragma once

/// @file neogsmedia.h
/// @brief Automation requests for the NeoGS card's media: SD card insert /
/// eject and flash save (CLI, WebAPI / MCP, Lua and Python share these).
///
/// The card runs on the machine's thread, and an SD eject closes the image
/// file the card may be reading, so a request is checked on the caller's
/// thread and then carried out on the machine's thread at an instruction
/// boundary (TimeTravelManager::SubmitMachineTask; while paused: when
/// execution continues). It is not TTD input: nothing is journaled.
///
/// While a TTD recording runs the machine's configuration is fixed: SD insert
/// and eject are refused. A flash save changes nothing in the machine and is
/// allowed; it snapshots the flash on the machine's thread.

#include <cstdint>
#include <string>

class EmulatorContext;

enum class NeoGSMediaResult : uint8_t
{
    Done,             ///< carried out at once (the machine was not running)
    Queued,           ///< handed to the machine's thread
    NoNeoGS,          ///< the GS slot holds no NeoGS card
    TtdRecording,     ///< refused: the configuration is fixed while recording
    ReplayOwnsInput,  ///< refused: a TTD replay owns the machine
    NoPath,           ///< sd_insert without an image path
    NoFile,           ///< the image does not exist
    Failed,           ///< carried out at once and failed (see the log)
};

NeoGSMediaResult NeoGSRequestSdInsert(EmulatorContext* context, const std::string& path);
NeoGSMediaResult NeoGSRequestSdEject(EmulatorContext* context);
NeoGSMediaResult NeoGSRequestFlashSave(EmulatorContext* context);

inline bool NeoGSMediaAccepted(NeoGSMediaResult r) { return r == NeoGSMediaResult::Done || r == NeoGSMediaResult::Queued; }
const char* NeoGSMediaResultText(NeoGSMediaResult r);
