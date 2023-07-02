#include "fdd.h"

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>
FDD::FDD(EmulatorContext* context) : _context(context)
{
    // TODO: remove debug
    _diskInserted = true;
}
/// endregion </Constructors / destructors>

/// region <Methods>

/// Method must be called at
void FDD::process()
{
    uint64_t frame = _context->emulatorState.frame_counter;
    uint64_t time = _context->pCore->GetZ80()->t;




    _lastFrame = frame;
    _lastTime = time;
}

void FDD::insertDisk(uint8_t* rawData)
{
    if (rawData)
    {
        _rawData = rawData;
        _diskInserted = true;
    }
}

void FDD::ejectDisk()
{
    _rawData = nullptr;
    _diskInserted = false;
}

/// endregion </Methods>