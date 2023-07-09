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

void FDD::insertDisk(DiskImage* diskImage)
{
    if (_diskImage)
    {
        _diskImage = diskImage;
        _diskInserted = true;
    }
}

void FDD::ejectDisk()
{
    _diskImage = nullptr;
    _diskInserted = false;
}

/// endregion </Methods>