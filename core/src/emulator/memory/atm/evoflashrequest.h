#pragma once

/// @file evoflashrequest.h
/// @brief Automation requests for the ZX-Evo's saved flash (EvoFlash persistence): status, save now, discard.
/// The CLI (`romflash`), the WebAPI (`/memory/rom/flash`, MCP `emulator_manage` rom_flash_*), Lua and Python share
/// these, as the NeoGS card's flash save (neogsmedia.h).
///
/// Save and discard run on the machine's thread at an instruction boundary (TimeTravelManager::SubmitMachineTask;
/// while paused: at once). Neither is TTD input: they touch only the file, never the machine. While a TTD replay
/// owns the machine they are refused. Discard deletes the file and asks for a ROM reload at the next reset, which
/// brings the shipped image back.

#include <cstdint>

#include "emulator/memory/atm/evoflash.h"

class EmulatorContext;

enum class EvoFlashResult : uint8_t
{
    Done,             ///< carried out
    Queued,           ///< handed to the machine's thread
    NoFlash,          ///< the machine's ROM is not a flash (only the ZX-Evo's: TS-Conf, ATM3)
    ReplayOwnsInput,  ///< refused: a TTD replay owns the machine
    Failed,           ///< carried out and failed: no ROM loaded yet, persistence off, or a file error (see the log)
};

/// False when the machine has no flash ROM
bool EvoFlashGetStatus(EmulatorContext* context, EvoFlash::PersistStatus& out);
EvoFlashResult EvoFlashRequestSave(EmulatorContext* context);
EvoFlashResult EvoFlashRequestDiscard(EmulatorContext* context);

inline bool EvoFlashAccepted(EvoFlashResult r) { return r == EvoFlashResult::Done || r == EvoFlashResult::Queued; }
const char* EvoFlashResultText(EvoFlashResult r);
