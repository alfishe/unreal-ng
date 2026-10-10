#include "nextregwrite.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace NextRegWriteControl
{
const char* DoorName(Door door)
{
    switch (door)
    {
        case Door::Port: return "port";
        case Door::Internal: return "internal";
        default: return "nextreg";
    }
}

bool ParseDoor(const std::string& text, Door& door)
{
    if (text.empty() || text == "nextreg")
        door = Door::NextReg;
    else if (text == "port")
        door = Door::Port;
    else if (text == "internal")
        door = Door::Internal;
    else
        return false;
    return true;
}

bool Parse(const std::string& regText, const std::string& valueText, uint8_t& reg, uint8_t& value, std::string& error)
{
    uint32_t number = 0;
    if (!ParseNextHex(regText, 0xFF, number))
    {
        error = "bad register '" + regText + "' (hex 00-FF: 07, 0x07, #07)";
        return false;
    }
    reg = static_cast<uint8_t>(number);
    if (!ParseNextHex(valueText, 0xFF, number))
    {
        error = "bad value '" + valueText + "' (hex 00-FF)";
        return false;
    }
    value = static_cast<uint8_t>(number);
    return true;
}

Result Write(Emulator* emulator, uint8_t reg, uint8_t value, Door door, const char* source)
{
    Result result;
    result.reg = reg;
    result.value = value;
    result.door = door;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pCore || !context->pCore->GetZ80())
    {
        result.error = "emulator not available";
        return result;
    }
    PortDecoder_Next* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    if (!decoder)
    {
        result.notNext = true;
        result.error = "not a ZX Spectrum Next";
        return result;
    }

    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            emulator->EditMemoryFromTool(source, [&]() {
                Z80::OutOfTimeScope outOfTime(*context->pCore->GetZ80());
                NextBoard& board = decoder->Board();
                result.previous = board.Stored(reg);
                context->toolAccessActive = true;
                switch (door)
                {
                    case Door::NextReg:
                        board.WriteNextReg(reg, value);
                        break;
                    case Door::Internal:
                        board.Write(reg, value);
                        break;
                    case Door::Port:
                    {
                        const uint16_t pc = context->pCore->GetZ80()->m1_pc;
                        decoder->DecodePortOut(PortDecoder_Next::kPortRegSelect, reg, pc);
                        decoder->DecodePortOut(PortDecoder_Next::kPortRegData, value, pc);
                        break;
                    }
                }
                context->toolAccessActive = false;
                result.after = board.Read(reg);
            });
        },
        500);
    if (where == Emulator::CoherentMoment::Busy)
    {
        result.busy = true;
        result.error = "no coherent moment within 500 ms (the emulator is stepping or changing state); try again";
        return result;
    }
    result.ok = true;
    result.moment = Emulator::CoherentMomentName(where);
    return result;
}

StateNode ToState(const Result& result)
{
    StateNode n = StateNode::Object();
    n["ok"] = result.ok;
    if (!result.ok)
    {
        n["error"] = result.error;
        return n;
    }
    n["reg"] = StringHelper::Format("0x%02X", result.reg);
    n["value"] = StringHelper::Format("0x%02X", result.value);
    n["previous"] = StringHelper::Format("0x%02X", result.previous);
    n["after"] = StringHelper::Format("0x%02X", result.after);
    n["door"] = DoorName(result.door);
    n["moment"] = result.moment;
    return n;
}
}  // namespace NextRegWriteControl
