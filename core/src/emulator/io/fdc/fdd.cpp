#include "fdd.h"

#include <random>
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>
FDD::FDD(EmulatorContext* context) : _context(context)
{
    /// region <Random track number on init>
    // Initialize random numbers generator
    std::random_device rd;
    std::mt19937 generator(rd());

    // Set distribution range within standard valid track number [0:80]
    std::uniform_int_distribution<size_t> trackValueDistribution(0, 80);

    // setTrack will set flags as well
    setTrack(trackValueDistribution(generator));

    /// endregion </Random track number on init>

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
    if (diskImage)
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