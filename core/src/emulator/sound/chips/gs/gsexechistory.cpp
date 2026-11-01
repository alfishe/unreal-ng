// The card CPU's execution history (generalsoundcard.h, exechistory.h): without a debugger the card runs its record
// loop (GSCardRunner::runToRecord), with one the recorder sits in front of the debugger's hooks

#include "emulator/sound/chips/gs/generalsoundcard.h"

#include <algorithm>
#include <cstdint>

#include "emulator/emulatorcontext.h"

void GeneralSoundCard::recordExecStep(uint16_t pc)
{
    if (_execContext && _execContext->ttdReplayActive)
        return;
    const CardPage page = mapCardAddress(pc);
    _execHistory->PushFast(ExecRecord{ _execContext ? _execContext->emulatorState.frame_counter : 0, pc,
                                       static_cast<uint8_t>(page.kind), page.page });
}

void GeneralSoundCard::recordExecIdle(uint16_t head, uint64_t iterations)
{
    if (!execRecording())
        return;
    const CardPage page = mapCardAddress(head);
    _execHistory->PushFast(ExecRecord{ _execContext ? _execContext->emulatorState.frame_counter : 0, head,
                                       static_cast<uint8_t>(page.kind), page.page,
                                       static_cast<uint32_t>(std::min<uint64_t>(iterations, UINT32_MAX)) });
}

bool GeneralSoundCard::execRecording() const
{
    return _execHistory && _execHistory->On() && !(_execContext && _execContext->ttdReplayActive);
}

void GeneralSoundCard::ExecRecorder::beforeStep(uint16_t pc)
{
    if (_card._execHistory && _card._execHistory->On())
        _card.recordExecStep(pc);
    if (inner)
        inner->beforeStep(pc);
}

bool GeneralSoundCard::bindHooks()
{
    _execBound = _execHistory && _execHistory->On();
    _execRecorder.inner = _debuggerHooks;
    return setDebugHooks(_execBound && _debuggerHooks ? &_execRecorder : _debuggerHooks);
}
