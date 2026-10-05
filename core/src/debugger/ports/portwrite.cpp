#include "portwrite.h"

#include "debugger/memory/memoryread.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

namespace PortWrite
{
Result Write(Emulator* emulator, uint16_t port, uint8_t value, const char* source)
{
    Result result;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pPortDecoder || !context->pCore || !context->pCore->GetZ80())
    {
        result.error = "emulator not available";
        return result;
    }

    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            emulator->EditMemoryFromTool(source, [&]() {
                Z80::OutOfTimeScope outOfTime(*context->pCore->GetZ80());
                context->toolAccessActive = true;
                context->pPortDecoder->DecodePortOut(port, value, context->pCore->GetZ80()->m1_pc);
                context->toolAccessActive = false;
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

bool Parse(const std::string& portText, const std::string& valueText, uint16_t& port, uint8_t& value,
           std::string& error)
{
    uint32_t number = 0;
    if (!MemoryRead::ParseNumber(portText, number) || number > 0xFFFF)
    {
        error = "bad port '" + portText + "' (0..#FFFF: 0x13AF, #13AF, 13AFh or decimal)";
        return false;
    }
    port = static_cast<uint16_t>(number);
    if (!MemoryRead::ParseNumber(valueText, number) || number > 0xFF)
    {
        error = "bad value '" + valueText + "' (0..#FF)";
        return false;
    }
    value = static_cast<uint8_t>(number);
    return true;
}
}  // namespace PortWrite
