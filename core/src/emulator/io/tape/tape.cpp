#include "stdafx.h"

#include "tape.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>

Tape::Tape(EmulatorContext *context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    reset();
}

/// endregion </Constructors / destructors>

/// region <Tape control methods>
void Tape::startTape()
{
    _tapeStarted = true;
}

void Tape::stopTape()
{
    _tapeStarted = false;
}
/// endregion </Tape control methods>


void Tape::reset()
{
    _tapeStarted = false;
    _tapePosition = 0LL;

    // Tape input bitstream related
    _currentTapeBlock = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _currentClockCount = 0;
};

uint8_t Tape::handlePortIn()
{
    uint8_t result = 0;
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;

    if (_tapeStarted)
    {
        size_t clockCount = cpu.clock_count;
        result = getPilotSample(clockCount) << 6;
        return result;
    }
    else
    {
        /// region <Imitate analogue noise>
        static uint16_t counter = 0;
        static uint8_t prevValue = 0;
        static uint16_t prngState = std::rand();

        if (counter == 0)
        {
            result = prngState & 0b0100'0000;

            prngState = std::rand();
        }

        // Galois LFSR with 16-bit register
        // The polynomial used in this implementation is x^16 + x^5 + x^3 + x^2 + 1, which has a maximal period of 2^16-1, or 65,535 values
        uint16_t bit = (prngState >> 0) ^ (prngState >> 2) ^ (prngState >> 3) ^ (prngState >> 5);
        prngState = (prngState >> 1) | (bit << 15);

        /*
        // Simple XORshift algorithm for PRNG
        prngState ^= prngState << 7;
        prngState ^= prngState >> 5;
        prngState ^= prngState << 3;
        */

        counter++;
        /// endregion </Imitate analogue noise>

        // If we just executed instruction at $0562 IN A,($FE)
        // And our PC is currently on $0564 RRA (which has opcode 0x1F)
        if ((cpu.pc & 0xFFFF) == 0x0564 && memory.GetPhysicalAddressForZ80Bank(0)[0x0564] == 0x1F)
            startTape();
    }

    return result;
}

void Tape::handlePortOut(bool value)
{
    // Fetch clock counter for precise timing
    size_t clockCount = _context->pCore->GetZ80()->clock_count;

    //MLOGINFO("%09ld, %d", clockCount, value);
    //MLOGINFO("[Out] [%09ld] Value: %d", clockCount, value);
}

/// region <Helper methods>
bool Tape::getTapeStreamBit(uint64_t clockCount)
{
    bool result = false;

    uint64_t deltaTime = clockCount - _currentClockCount;

    if (_tapeStarted)
    {
        if (_currentTapeBlock != UINT64_MAX)
        {

        }
        else
        {
            // End of tape
            // TODO: Rewind and stop
        }
    }

    // Remember last used clock count for the next iteration
    _currentClockCount = clockCount;

    return result;
}
/// endregion </Helper methods>

// TODO: just experimentation method
bool Tape::getPilotSample(size_t clockCount)
{
    static uint16_t counter = 0;
    static constexpr uint16_t PILOT_HALF_PERIOD = 2168;
    static constexpr uint16_t PILOT_PERIOD = PILOT_HALF_PERIOD * 2;

    size_t normalizedToPeriod = (clockCount % PILOT_PERIOD);
    bool result = (normalizedToPeriod < PILOT_HALF_PERIOD);

    /*
    bool result = (counter < PILOT_HALF_PERIOD);
    counter += (tState - counter);

    if (counter >= PILOT_PERIOD)
    {
        counter = 0;
    }

    if (result)
    {
        counter = counter;
    }
    else
    {
        counter = counter;
    }
    */

    size_t frameCounter = _context->emulatorState.frame_counter;
    size_t tState = _context->pCore->GetZ80()->t;
    MLOGINFO("Frame: %04d tState: %05d clockCount: %08d pilot: %d", frameCounter, tState, clockCount, result);

    return result;
}