#include "debugger/breakpoints/chipregister.h"

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

void ReportChipRegisterWrite(EmulatorContext* context, SoundChipKind kind, uint8_t unit, uint16_t reg, uint8_t value)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    if (!debug || !context->pEmulator || !context->pCore)
        return;
    // Out of the debug mode only the taps hear it (a tap never needs the debug mode)
    BreakpointManager* breakpoints = debug->GetBreakpointsManager();
    const bool stops = context->pCore->GetZ80()->isDebugMode;
    if (!stops && !breakpoints->HasChipRegisterTaps())
        return;
    const uint16_t id = breakpoints->HandleChipRegister(kind, unit, reg, value, stops);
    if (id != BRK_INVALID) [[unlikely]]
        context->pEmulator->OnBreakpointHit(id, reg, BreakpointHitKind::ChipRegister);
}
