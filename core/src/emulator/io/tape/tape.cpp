#include "stdafx.h"

#include "tape.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "loaders/tape/loader_tap.h"

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

    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _currentClockCount = 0;
}
/// endregion </Tape control methods>


void Tape::reset()
{
    _tapeStarted = false;
    _tapePosition = 0LL;

    // Tape input bitstream related
    _tapeBlocks = std::vector<TapeBlock>();
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _currentClockCount = 0;
};

/// region <Port events>

uint8_t Tape::handlePortIn()
{
    uint8_t result = 0;
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;

    if (_tapeStarted)
    {
        size_t clockCount = cpu.clock_count;

        result = getTapeStreamBit(clockCount) << 6;
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
        if (cpu.pc == 0x0564 && memory.IsCurrentROM48k() && memory.GetPhysicalAddressForZ80Page(0)[0x0564] == 0x1F)
        {
            LoaderTAP loader(_context);
            _tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/action.tap");

            startTape();
        }
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

/// endregion </Port events>

/// region <Emulation events>

/// Prepare for next video frame start
/// If we have previous tape block played, then we can generate bitstream for the next block
void Tape::handleFrameStart()
{
    // Fetch clock counter for precise timing
    size_t clockCount = _context->pCore->GetZ80()->clock_count;

    if (_tapeStarted && !_tapeBlocks.empty())
    {
        // Tape is just loaded, we need setup fields
        if (_currentTapeBlock == nullptr && _currentTapeBlockIndex == UINT64_MAX)
        {
            _currentTapeBlock = &_tapeBlocks[0];
            _currentTapeBlockIndex = 0;
            _currentPulseIdxInBlock = 0;
            _currentOffsetWithinPulse = 0;

            // Generating bit-stream related data
            generateBitstreamForStandardBlock(*_currentTapeBlock);

            // Record current clock
            _currentClockCount = clockCount;
        }
        // Just switched to next block, need to generate bit stream for it
        else if (_currentTapeBlockIndex < _tapeBlocks.size() && _currentTapeBlock == nullptr)
        {
            // Getting new TapeBlock
            _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];

            if (_currentTapeBlock)
            {
                // Generating bit-stream related data
                generateBitstreamForStandardBlock(*_currentTapeBlock);
            }
            else
            {
                // Error. There must be no nullable blocks
                throw std::logic_error("Tape::handleFrameStart() null TapeBlock found");
            }
        }
        else if (_currentTapeBlockIndex == UINT64_MAX)
        {
            // We've depleted all available blocks
            stopTape();
        }
    }
}

void Tape::handleFrameEnd()
{
    // Fetch clock counter for precise timing
    size_t clockCount = _context->pCore->GetZ80()->clock_count;
}

/// endregion </Emulation events>

/// region <Helper methods>

bool Tape::getTapeStreamBit(uint64_t clockCount)
{
    static bool result = false;

    uint64_t deltaTime = clockCount - _currentClockCount;

    if (_tapeStarted && _currentTapeBlock && _currentTapeBlockIndex != UINT64_MAX)
    {
        // Forward playback for the whole deltaTime period
        for (int i = 0; i < deltaTime; i++)
        {
            // Determine position within bit stream
            // Find correspondent pulse timings record and then count up to its value
            TapeBlock &block = *_currentTapeBlock;
            uint32_t currentPulseDuration = block.edgePulseTimings[_currentOffsetWithinPulse];

            // Create signal edge by inverting tape bit
            if (currentPulseDuration > 0 && _currentPulseIdxInBlock == 0)
            {
                result = !result;
            }

            /// region <Perform repositioning for next bit in stream>
            _currentPulseIdxInBlock++;
            if (_currentPulseIdxInBlock >= currentPulseDuration)
            {
                // Pulse duration finished, switch to next
                _currentOffsetWithinPulse++;
                _currentPulseIdxInBlock = 0;

                if (_currentOffsetWithinPulse >= block.edgePulseTimings.size())
                {
                    // We're depleted all pulses within this block. Switching to next one
                    _currentTapeBlockIndex++;
                    _currentTapeBlock = nullptr;
                    _currentOffsetWithinPulse = UINT64_MAX;
                    _currentPulseIdxInBlock = UINT64_MAX;
                    break;
                }
            }
            /// endregion </Perform repositioning for next bit in stream>
        }
    }

    // Remember last used clock count for the next iteration
    _currentClockCount = clockCount;

    return result;
}

/// Generate bitstream assistive data for the TapeBlock data
/// @param tapeBlock Reference to single TapeBlock object
/// @return Result whether generating process finished successfully or not
bool Tape::generateBitstreamForStandardBlock(TapeBlock& tapeBlock)
{
    bool result = false;

    bool isHeader = tapeBlock.type == TAP_BLOCK_FLAG_HEADER;

    size_t totalBlockDuration = generateBitstream(tapeBlock,
                                                  PILOT_TONE_HALF_PERIOD,
                                                  PILOT_SYNCHRO_1,
                                                  PILOT_SYNCHRO_1,
                                                  ZERO_ENCODE_HALF_PERIOD,
                                                  ONE_ENCODE_HALF_PERIOD,
                                                  isHeader ? PILOT_DURATION_HEADER : PILOT_DURATION_DATA,
                                                  1000);

    if (totalBlockDuration > 0)
    {
        result = true;
    }

    return result;
}

size_t Tape::generateBitstream(TapeBlock& tapeBlock,
                               uint16_t pilotHalfPeriod_tStates,
                               uint16_t synchro1_tStates,
                               uint16_t synchro2_tStates,
                               uint16_t zeroEncodingHalfPeriod_tState,
                               uint16_t oneEncodingHalfPeriod_tStates,
                               size_t pilotLength_periods,
                               size_t pause_ms)
{
    size_t result = 0;
    size_t len = tapeBlock.data.size();

    // Calculate collection size to fit all edge time intervals
    size_t resultSize = 0;
    resultSize += (pilotLength_periods * 2);    // Each pilot signal period is encoded as 2 edges
    resultSize += 2;                            // Two sync pulses at the end of pilot
    resultSize += (len * 8 * 2);                // Each byte split to bits and each bit encoded as 2 edges
    if (pause_ms > 0)
        resultSize += 1;                        // Pause is just a marker so single edge is sufficient

    tapeBlock.edgePulseTimings.reserve(resultSize);

    /// region <Pilot tone + sync>

    if (pilotLength_periods > 0)
    {
        // Required number of pilot periods
        // Calling code determines it based on block type: header or data
        for (size_t i = 0; i < pilotLength_periods; i++)
        {
            tapeBlock.edgePulseTimings.push_back(pilotHalfPeriod_tStates);

            result += pilotHalfPeriod_tStates;
        }

        // Sync pulses at the end of pilot
        tapeBlock.edgePulseTimings.push_back(synchro1_tStates);
        tapeBlock.edgePulseTimings.push_back(synchro2_tStates);

        result += synchro1_tStates;
        result += synchro2_tStates;
    }

    /// endregion </Pilot tone + sync>

    /// region <Data paramBytes>

    for (size_t i = 0; i < len; i++)
    {
        // Extract bits from input data byte and add correspondent bit encoding length to image array
        for (uint8_t bitMask = 0x80; bitMask != 0; bitMask >>= 1)
        {
            bool bit = (tapeBlock.data[i] & bitMask) != 0;
            uint16_t bitEncoded = bit ? oneEncodingHalfPeriod_tStates : zeroEncodingHalfPeriod_tState;

            // Each bit is encoded by two edges
            tapeBlock.edgePulseTimings.push_back(bitEncoded);
            tapeBlock.edgePulseTimings.push_back(bitEncoded);

            result += bitEncoded;
        }
    }

    /// endregion </Data paramBytes>


    /// region <Pause>

    if (pause_ms)
    {
        // Pause doesn't require any encoding, just a time mark after the delay
        size_t pauseDuration = pause_ms * 3500;
        tapeBlock.edgePulseTimings.push_back(pauseDuration);

        result += pauseDuration;
    }

    /// endregion </Pause>

    tapeBlock.totalBitstreamLength = result;

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