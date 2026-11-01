// The card CPU's execution history (generalsoundcard.h, exechistory.h): the recorder the card runs with while the
// history is on, in front of a debugger's hooks

#include "emulator/sound/chips/gs/generalsoundcard.h"

#include "emulator/emulatorcontext.h"

void GeneralSoundCard::ExecRecorder::beforeStep(uint16_t pc)
{
    history::DeviceHistory<ExecRecord>* history = _card._execHistory;
    if (history && history->On() && !(_card._execContext && _card._execContext->ttdReplayActive))
    {
        const CardPage page = _card.mapCardAddress(pc);
        history->PushFast(ExecRecord{ _card._execContext ? _card._execContext->emulatorState.frame_counter : 0, pc,
                                      static_cast<uint8_t>(page.kind), page.page });
    }
    if (inner)
        inner->beforeStep(pc);
}

bool GeneralSoundCard::bindHooks()
{
    _execBound = _execHistory && _execHistory->On();
    _execRecorder.inner = _debuggerHooks;
    return setDebugHooks(_execBound ? &_execRecorder : _debuggerHooks);
}
