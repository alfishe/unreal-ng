#pragma once

/// @file midicontrol.h
/// @brief MIDI control for every surface (ZX-MultiSound tdd-integration.md §5, MS-6): the WebAPI
/// (POST /control/audio/midi), the CLI (`midi panic`), MCP (through the WebAPI), Lua / Python (`midi_panic()`) and the
/// Qt MIDI view call Execute with an action and show the reply.
///
/// Actions:
///   panic   every voice of every MIDI synthesizer the slots built (the ZX-MultiSound's SAM2695) stops, as All Sound
///           Off on all 16 parts; the controllers, programs and the MIDI stream in progress stay. Applied at the next
///           instruction boundary as a TTD live input (TTDInputKind::MidiPanic): journaled while recording and
///           replayed, so a session with a panic replays the same. Refused while a TTD replay owns the input.
///
/// The bank is configuration ([MIDI] Bank=), changed like a slot option: plan, then a restart (slots Q6).

#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;

struct MidiControlReply
{
    bool ok = false;
    int httpStatus = 200;
    std::string action;
    std::string message;

    /// ok, action, message
    StateNode ToValue() const;
};

class MidiControl
{
public:
    static MidiControlReply Execute(EmulatorContext* context, const std::string& action);
};
