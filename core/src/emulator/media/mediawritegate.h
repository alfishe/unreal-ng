#pragma once

/// @file mediawritegate.h
/// @brief The one rule for host persistence while time travel replays history
/// (FR-20): nothing the replayed machine does may write a host file. Media
/// keep guest writes in memory (HostWriteHold, MediaManager::HoldHostWrites),
/// floppy write-through waits for the next live frame, device captures skip.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.7

#include "emulator/emulatorcontext.h"

class MediaWriteGate
{
public:
    static bool HostWritesAllowed(const EmulatorContext& context) { return !context.ttdReplayActive; }
};
