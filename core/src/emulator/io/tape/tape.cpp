#include "stdafx.h"

#include "tape.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/tape/loader_tap.h"

/// region <Constructors / destructors>

Tape::Tape(EmulatorContext *context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    reset();

    /// region <Debug functionality>
    _logFile = fopen("unreal-tape-load-log.txt", "w"); // open the file in write mode
    /// endregion </Debug functionality>
}

Tape::~Tape()
{
    /// region <Debug functionality>
    fclose(_logFile);
    /// endregion </Debug functionality>
}

/// endregion </Constructors / destructors>

/// region <Tape control methods>
void Tape::startTape()
{
    _tapeStarted = true;
    _muteEAR = true;
}

void Tape::stopTape()
{
    _tapeStarted = false;
    _muteEAR = false;

    // Reset all tape-related fields and free up blocks memory
    _tapeBlocks = std::vector<TapeBlock>();
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

    CONFIG& config = _context->config;
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;

    const uint32_t tState = _context->pCore->GetZ80()->t;
    uint8_t prevPortValue = _context->emulatorState.pFE;

    if (_tapeStarted)
    {
        size_t clockCount = cpu.clock_count;

        bool tapeBit = getTapeStreamBit(clockCount);
        result = (uint8_t)tapeBit << 6;

        /// region <Debug functionality
        std::string logLine = StringHelper::Format("[%0d] Bit: %1d\n", clockCount, tapeBit);
        fwrite(logLine.c_str(), 1, logLine.size(), _logFile);
        /// endregion </Debug functionality

        // Only mic sound is heard to prevent clicks from keyboard polling out (#FE)
        int16_t micSample = tapeBit ? 1000 : -1000;
        int16_t sample = _dcFilter.filter(micSample);   // Apply LPF filtering to remove high-frequency noise
        _context->pSoundManager->updateDAC(tState, sample, sample);
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
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/ELITE_ATOSSOFT.tap");
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/ELITE_NICOLAS_RODIONOV.tap");
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/traffic_lights.tap");
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/action.tap");
            _tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/IntTest+.tap");
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/earshaver.tap");
            //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/lphp.tap");

            startTape();
        }
    }

    return result;
}

void Tape::handlePortOut(uint8_t value)
{
    // Fetch clock counter for precise timing
    size_t clockCount = _context->pCore->GetZ80()->clock_count;
    uint32_t tState = _context->pCore->GetZ80()->t;

    bool outBit = value & 0b0001'0000;
    bool micBit = value & 0b0000'1000;

    int16_t sample;
    if (!_muteEAR)
    {
        // Use only EAR output sound
        int16_t earSample = outBit ? 3000 : -3000;
        sample = _dcFilter.filter(earSample);   // Apply LPF filtering to remove high-frequency noise;

        _context->pSoundManager->updateDAC(tState, sample, sample);
    }
    else // Ignore outputs while tape is playing
    {
        // Use only tape sound
        //int16_t micSample = micBit ? 1000: -1000;
        //sample = micSample;
    }

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

                // Clear bit-stream data from previous block
                TapeBlock& previousBlock = _tapeBlocks[_currentTapeBlockIndex - 1];
                previousBlock.totalBitstreamLength = 0;
                previousBlock.edgePulseTimings = std::vector<uint32_t>();
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
        // Determine position within bit stream
        // Find correspondent pulse timings record and then count up to its value
        TapeBlock &block = *_currentTapeBlock;
        uint32_t currentPulseDuration = block.edgePulseTimings[_currentOffsetWithinPulse];

        // Forward playback for the whole deltaTime period
        for (int i = 0; i < deltaTime; i++)
        {
            // Create signal edge by inverting tape bit
            if (currentPulseDuration > 0 && _currentPulseIdxInBlock == 0)
            {
                result = !result;
            }

            if (_currentPulseIdxInBlock + deltaTime > currentPulseDuration)
            {
                size_t pulsesToSkip = currentPulseDuration - _currentPulseIdxInBlock;

                _currentPulseIdxInBlock += pulsesToSkip;
                i += pulsesToSkip;
            }
            else
            {
                _currentPulseIdxInBlock++;
            }

            /// region <Perform repositioning for next bit in stream>
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
                    _currentOffsetWithinPulse = 0;
                    _currentPulseIdxInBlock = 0;
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
                                                  PILOT_SYNCHRO_2,
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