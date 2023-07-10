#pragma once

#include "stdafx.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

class TestTimingHelper
{
    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    TestTimingHelper(EmulatorContext* context) : _context(context) {};
    virtual ~TestTimingHelper() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void resetClock()
    {
        Z80* z80 = _context->pCore->GetZ80();
        z80->t = 0;
        _context->emulatorState.t_states = 0;
    }

    void forward(size_t tStates)
    {
        Z80* z80 = _context->pCore->GetZ80();
        uint32_t frameTStates = z80->t; // Store original in-frame t-state counter

        _context->emulatorState.t_states += tStates;
        z80->t += tStates;
    }
    /// endregion </Methods>
};
