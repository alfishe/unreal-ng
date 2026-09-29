#pragma once

/// @file neogsmedia.h
/// @brief Automation requests for the NeoGS card's media: SD card insert /
/// eject and flash save (CLI, WebAPI / MCP, Lua and Python share these).
///
/// The SD card is the media manager's slot `sd.ngs` (the same slot as the
/// `media` commands and the GUI's media panel): a request is checked on the
/// caller's thread and applied at the next frame boundary, or at once while
/// the machine is not running. An image file or a host folder can go in.
/// Without a media manager (bare contexts) the card opens the image itself.
/// A flash save is carried out on the machine's thread at an instruction
/// boundary (TimeTravelManager::SubmitMachineTask; while paused: when
/// execution continues). Neither is TTD input: nothing is journaled.
///
/// While a TTD recording runs the machine's configuration is fixed: SD insert
/// and eject are refused (the media manager's rule for every slot). A flash
/// save changes nothing in the machine and is allowed.

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
    NoFile,           ///< neither an image nor a folder at that path
    Failed,           ///< carried out at once and failed (see the log)
};

NeoGSMediaResult NeoGSRequestSdInsert(EmulatorContext* context, const std::string& path);
NeoGSMediaResult NeoGSRequestSdEject(EmulatorContext* context);
NeoGSMediaResult NeoGSRequestFlashSave(EmulatorContext* context);

inline bool NeoGSMediaAccepted(NeoGSMediaResult r) { return r == NeoGSMediaResult::Done || r == NeoGSMediaResult::Queued; }
const char* NeoGSMediaResultText(NeoGSMediaResult r);
