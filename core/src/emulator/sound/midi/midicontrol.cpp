#include "midicontrol.h"

#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"

StateNode MidiControlReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = ok;
    value["action"] = action;
    value["message"] = message;
    return value;
}

MidiControlReply MidiControl::Execute(EmulatorContext* context, const std::string& action)
{
    MidiControlReply reply;
    reply.action = action;
    if (action != "panic")
    {
        reply.httpStatus = 400;
        reply.message = "unknown MIDI action '" + action + "' (panic)";
        return reply;
    }
    const StateNode midi = DeviceState::Midi(context);
    if (const StateNode* available = midi.find("available"); available == nullptr || !available->b)
    {
        reply.httpStatus = 404;
        const StateNode* description = midi.find("description");
        reply.message = description != nullptr ? description->s : "no MIDI synthesizer";
        return reply;
    }
    ttd::TTDInputEvent event;
    event.kind = ttd::TTDInputKind::MidiPanic;
    if (context->pTimeTravelManager == nullptr || !context->pTimeTravelManager->SubmitLiveInput(event))
    {
        reply.httpStatus = 409;
        reply.message = "MIDI panic refused: a TTD replay owns the input";
        return reply;
    }
    reply.ok = true;
    reply.message = "MIDI panic: every voice stops (applied at the next instruction boundary)";
    return reply;
}
