#include "fdd.h"

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Methods>
void FDD::process()
{
    uint64_t frame = _context->emulatorState.frame_counter;
    uint64_t time = _context->pCore->GetZ80()->t;




    _lastFrame = frame;
    _lastTime = time;
}
/// endregion </Methods>