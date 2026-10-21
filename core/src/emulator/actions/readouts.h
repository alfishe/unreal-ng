#pragma once

/// @file readouts.h
/// @brief The readout vocabulary (Stream Deck integration protocol.md §3): the values a surface draws from - one
/// producer each, every one from published state, never a read that holds the emulation thread or waits for it.
///
/// | Readout | Value |
/// |:--|:--|
/// | `ttd.position` | the TTD `position` verb: frame, tinframe, first / end, state, history, paused, fps (S2's cursor) |
/// | `run.state` | {state: running / paused / stopped, speed (the host multiplier 1..16), turbo} |
/// | `machine` | {model, ram_kb, index} |
/// | `media.tape` | {loaded, playing, block, blocks, progress (blocks done / blocks), name} |
/// | `media.disk` | {drives: [{letter, loaded, name, busy (the motor), protect}]} |
/// | `audio.mute` | {muted} (the host has no master volume: the mixer's master mutes only) |
/// | `debug.mode` | {enabled} |
/// | `toggle.<id>` | {on} for every toggle of the catalogue (`toggle.feature.<name>` for the features) |
///
/// Names are matched exactly or by prefix (`media.*`, `toggle.*`; `*` = all). The WebSocket `surface` topic (S4)
/// reads them on its tick through ReadReadouts and sends what changed; the REST / CLI / script `readouts` verb shows
/// them once.

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace actions
{

struct ReadoutSet
{
    StateNode values = StateNode::Object();  ///< name -> value
    std::vector<std::string> unknown;        ///< asked names that match no readout (protocol: `error not_found`)
};

/// The fixed names (without the toggles)
const std::vector<std::string>& BaseReadoutNames();
/// Every name the instance has now: the base ones and a `toggle.*` per toggle (features listed by name)
std::vector<std::string> ReadoutNames(Emulator& emulator);
/// Does `name` match `pattern` (exact, `prefix.*`, `*`)
bool ReadoutMatches(const std::string& pattern, const std::string& name);

/// One readout's value now; Null for an unknown name
StateNode ReadReadout(Emulator& emulator, const std::string& name);
/// The readouts the patterns name (none: all) on `instanceId`
ReadoutSet ReadReadouts(const std::string& instanceId, const std::vector<std::string>& patterns);

}  // namespace actions
