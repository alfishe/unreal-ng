#pragma once

#include "stdafx.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

// Addons to Gtest matchers
#define EXPECT_IN_RANGE(VAL, MIN, MAX) \
    EXPECT_GE((VAL), (MIN));           \
    EXPECT_LE((VAL), (MAX))

#define ASSERT_IN_RANGE(VAL, MIN, MAX) \
    ASSERT_GE((VAL), (MIN));           \
    ASSERT_LE((VAL), (MAX))

class TestTimingHelper
{
    /// region <Constants>
public:
    static constexpr size_t Z80_FREQUENCY = 3.5 * 1'000'000;
    static constexpr size_t TSTATES_PER_MS = Z80_FREQUENCY / 1000;
    /// endregion </Constants>

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

    /// region <Static helper methods>
public:
    static size_t convertTStatesToMs(size_t tStates)
    {
        size_t result = tStates / TSTATES_PER_MS;

        return result;
    }
    /// endregion <Static helper methods>
};
