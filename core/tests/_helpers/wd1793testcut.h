#pragma once

#include <array>

#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"

/// WD1793CUT for a controller a test builds by hand (no Emulator). WD1793's constructor creates 4 FDDs in
/// coreState.diskDrives[]; in a machine Emulator::Release frees them, here nobody would. This one frees the
/// drives its own constructor made. A test may build several controllers on one context (each overwrites
/// diskDrives[]), so it keeps its own 4 rather than freeing whatever the context holds at the end.
/// No eject: the FDD does not own the image, and a test-local DiskImage may be gone already
class WD1793TestCUT : public WD1793CUT
{
public:
    explicit WD1793TestCUT(EmulatorContext* context) : WD1793CUT(context), _driveContext(context)
    {
        for (size_t i = 0; i < _ownDrives.size(); i++)
            _ownDrives[i] = context->coreState.diskDrives[i];
    }

    ~WD1793TestCUT() override
    {
        for (size_t i = 0; i < _ownDrives.size(); i++)
        {
            // ~WD1793 runs next and touches every drive still in the context: take ours out first
            if (_driveContext->coreState.diskDrives[i] == _ownDrives[i])
                _driveContext->coreState.diskDrives[i] = nullptr;
            delete _ownDrives[i];
        }
    }

    WD1793TestCUT(const WD1793TestCUT&) = delete;
    WD1793TestCUT& operator=(const WD1793TestCUT&) = delete;

private:
    EmulatorContext* _driveContext = nullptr;
    std::array<FDD*, 4> _ownDrives = {};
};
