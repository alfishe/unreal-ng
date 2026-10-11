#include "nextnmi.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace NextNmiControl
{
Result Trigger(Emulator* emulator, const std::string& source)
{
    Result result;
    result.source = source;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    PortDecoder_Next* decoder = context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
    if (!decoder || !context->pCore || !context->pCore->GetZ80())
    {
        result.notNext = true;
        result.reason = "not a ZX Spectrum Next";
        return result;
    }
    if (source != "m1" && source != "drive" && source != "pulse")
    {
        result.badSource = true;
        result.reason = "source '" + source + "': m1, drive or pulse";
        return result;
    }
    // The CPU parked at an instruction boundary: the request lands between two instructions
    Emulator::CpuHold hold(*emulator, "next nmi");
    result.accepted = decoder->TriggerNmi(source, result.reason);
    result.ok = true;
    return result;
}

StateNode ToState(const Result& result)
{
    StateNode n = StateNode::Object();
    n["ok"] = result.ok;
    if (!result.ok)
    {
        n["error"] = result.reason;
        return n;
    }
    n["accepted"] = result.accepted;
    n["source"] = result.source;
    if (!result.accepted)
        n["reason"] = result.reason;
    return n;
}
}  // namespace NextNmiControl
