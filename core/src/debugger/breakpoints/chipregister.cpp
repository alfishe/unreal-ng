#include "debugger/breakpoints/chipregister.h"

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

void ReportChipRegisterWrite(EmulatorContext* context, SoundChipKind kind, uint8_t unit, uint16_t reg, uint8_t value)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    if (!debug || !context->pEmulator || !context->pCore || !context->pCore->GetZ80()->isDebugMode)
        return;
    const uint16_t id = debug->GetBreakpointsManager()->HandleChipRegister(kind, unit, reg, value);
    if (id != BRK_INVALID) [[unlikely]]
        context->pEmulator->OnBreakpointHit(id, reg, BreakpointHitKind::ChipRegister);
}
