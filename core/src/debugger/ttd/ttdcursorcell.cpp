#include "debugger/ttd/ttdcursorcell.h"

#include "emulator/actions/contextstate.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace ttd
{

double TTDFrameRate(const EmulatorContext* context)
{
    if (context == nullptr)
        return 50.0;
    const uint32_t frequency = context->emulatorState.base_z80_frequency;
    const uint32_t frame = context->GetBaseFrameTStates();
    if (frequency == 0 || frame == 0)
        return 50.0;
    return static_cast<double>(frequency) / static_cast<double>(frame);
}

void NoteHistoryChange(const EmulatorContext* context, bool history)
{
    const Emulator* emulator = context != nullptr ? context->pEmulator : nullptr;
    if (emulator == nullptr)
        return;
    actions::ContextState::Instance().HistoryChanged(emulator->GetId(), history);
}

}  // namespace ttd
