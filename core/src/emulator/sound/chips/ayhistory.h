#pragma once

/// @file ayhistory.h
/// @brief The AY register-write history (debugger/history/devicehistory.h): one per machine, owned by SoundManager and
/// named by the registry device the AY ports belong to (the built-in `ay`, or the card that shadows it: `ay-socket`,
/// `zxbus.1`). Every device that takes the AY port writes records into it: the TurboSound pair, the TSFM, the Next's
/// three AYs (NextAudio).

#include <cstdint>
#include <string>

#include "debugger/history/devicehistory.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

class EmulatorContext;

/// The standby ring size: the AY log's default capacity (AYLogAnalyzer)
constexpr size_t kAYHistoryRing = 4096;

/// The registry id of the device that takes the AY register select (OUT #FFFD) on this machine; `ay` when the
/// registry names none
std::string AYHistoryDevice(const EmulatorContext* context);
/// Whether the registry has a device on the AY register select
bool AYHistoryDeclared(const EmulatorContext* context);

/// One AY port write, after the device applied it: PC of the OUT, the frame and its T-state from the CPU. Skipped
/// while TTD replays silently (a seek re-executes what the history already has). The caller checks history.On()
void RecordAYWrite(const EmulatorContext* context, AYHistory& history, uint16_t port, uint8_t value, uint8_t chip,
                   uint8_t reg);
