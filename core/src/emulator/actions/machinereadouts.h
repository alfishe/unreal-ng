#pragma once

/// @file machinereadouts.h
/// @brief The machine area's readouts (readouts.h vocabulary), what a key or the strip shows for the actions of
/// hostactions_machine.cpp - each from published state, never a read that holds the emulation thread:
///
/// | Readout | Value |
/// |:--|:--|
/// | `machine.previous` | {model, ram_kb} the model mach.previous starts (null fields: none yet) |
/// | `machine.switches` | {switches: [{name, on}]} the front-panel switches (none on most machines) |
/// | `media.recent` | {revision, kinds: {snapshot: [{index, kind, name, path, time}], ...}, all: [...]} (RecentFiles) |
/// | `media.rzx` | {loaded, active, name, state, frame, total_frames, progress, desyncs} |
/// | `input.connectors` | {connectors: [{id, kind, device, fixed, port?}]} the joystick connectors |
/// | `cap.record` | {available, recording, paused, frames, seconds, bytes, output} the video recording |

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace actions
{

const std::vector<std::string>& MachineReadoutNames();
/// One of them now; Null for a name that is not one
StateNode ReadMachineReadout(Emulator& emulator, const std::string& name);

}  // namespace actions
