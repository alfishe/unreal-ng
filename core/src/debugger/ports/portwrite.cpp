#include "portwrite.h"

#include "debugger/memory/memoryread.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

#include <functional>

namespace PortWrite
{
namespace
{
/// Runs `access` on the decoder as a tool edit at a coherent moment (the shared half of Write and Read)
Result ToolPortAccess(Emulator* emulator, const char* source, const std::function<void(EmulatorContext&)>& access)
{
    Result result;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pPortDecoder || !context->pCore || !context->pCore->GetZ80())
    {
        result.error = "emulator not available";
        return result;
    }

    std::string refused;
    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            refused = emulator->EditMemoryFromTool(source, [&]() {
                Z80::OutOfTimeScope outOfTime(*context->pCore->GetZ80());
                context->toolAccessActive = true;
                access(*context);
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
    if (!refused.empty())
    {
        result.error = refused;   // the recorded past
        return result;
    }
    result.ok = true;
    result.moment = Emulator::CoherentMomentName(where);
    return result;
}
}  // namespace

Result Write(Emulator* emulator, uint16_t port, uint8_t value, const char* source)
{
    return ToolPortAccess(emulator, source, [&](EmulatorContext& context) {
        context.pPortDecoder->DecodePortOut(port, value, context.pCore->GetZ80()->m1_pc);
    });
}

Result Read(Emulator* emulator, uint16_t port, uint8_t& value, const char* source)
{
    uint8_t read = 0xFF;
    Result result = ToolPortAccess(emulator, source, [&](EmulatorContext& context) {
        read = context.pPortDecoder->DecodePortIn(port, context.pCore->GetZ80()->m1_pc);
    });
    if (result.ok)
        value = read;
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
