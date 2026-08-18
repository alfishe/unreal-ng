#include "tape.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/spectrumconstants.h"
#include "loaders/tape/loader_tap.h"
#include "stdafx.h"

/// region <Constructors / destructors>

Tape::Tape(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    reset();
}

Tape::~Tape() {}

/// endregion </Constructors / destructors>

/// region <Tape control methods>
void Tape::startTape()
{
    _tapeStarted = true;
    _muteEAR = true;
    _lastTapeBit = false;
    _framesSinceLastRead = 0;
    _initialErrNr = _context->pMemory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
    MLOGINFO("Tape started, initial ERR_NR=0x%02X", _initialErrNr);
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
    _lastTapeBit = false;
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
    _lastTapeBit = false;
};

/// region <Port events>

uint8_t Tape::handlePortIn()
{
    uint8_t result = 0;

    [[maybe_unused]] CONFIG& config = _context->config;
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;

    const uint32_t tState = _context->pCore->GetZ80()->t;
    [[maybe_unused]] uint8_t prevPortValue = _context->emulatorState.pFE;

    // Scale t-state by speed multiplier for correct audio timing
    [[maybe_unused]] uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    [[maybe_unused]] uint32_t scaledTState = tState * speedMultiplier;

    if (_tapeStarted)
    {
        // Reset frame counter - loader is actively reading
        _framesSinceLastRead = 0;

        // Use monotonic counter for tape timing (t_states + t)
        uint64_t clockCount = _context->emulatorState.t_states + cpu.t;

        bool tapeBit = getTapeStreamBit(clockCount);
        result = (uint8_t)tapeBit << 6;
    }
    else
    {
        /// region <Imitate analogue noise>
        static uint16_t counter = 0;
        [[maybe_unused]] static uint8_t prevValue = 0;
        static uint16_t prngState = std::rand();

        if (counter == 0)
        {
            result = prngState & 0b0100'0000;

            prngState = std::rand();
        }

        // Galois LFSR with 16-bit register
        // The polynomial used in this implementation is x^16 + x^5 + x^3 + x^2 + 1, which has a maximal period of
        // 2^16-1, or 65,535 values
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
        // Check ROM content directly - works for both 48K and 128K modes
        uint8_t* romBank = memory.GetPhysicalAddressForZ80Page(0);
        if (cpu.pc == 0x0564 && romBank && romBank[0x0564] == 0x1F)
        {
            LoaderTAP loader(_context);

            if (_context->coreState.tapeFilePath.empty())
            {
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/ELITE_ATOSSOFT.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/ELITE_NICOLAS_RODIONOV.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/traffic_lights.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/action.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/IntTest+.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/earshaver.tap");
                //_tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/lphp.tap");
                _tapeBlocks = loader.loadTAP("../../../tests/loaders/tap/AYtest_v0.2.tap");
            }
            else
            {
                _tapeBlocks = loader.loadTAP(_context->coreState.tapeFilePath);
            }

            startTape();
        }
    }

    return result;
}

void Tape::handlePortOut([[maybe_unused]] uint8_t value)
{
    // Hardware ULA Port #FE OUT (EAR/MIC bits) audio synthesis is handled
    // directly by Beeper::handlePortOut(). Tape audio input (during tape loading)
    // is processed in handlePortIn().
}

/// endregion </Port events>

/// region <Emulation events>

/// Prepare for next video frame start
/// If we have previous tape block played, then we can generate bitstream for the next block
void Tape::handleFrameStart()
{
    // Use monotonic counter for tape timing (t_states + t)
    uint64_t clockCount = _context->emulatorState.t_states + _context->pCore->GetZ80()->t;

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

void Tape::handleStep()
{
    if (!_tapeStarted)
        return;

    Z80& cpu = *_context->pCore->GetZ80();
    const uint32_t tState = cpu.t;
    uint64_t clockCount = _context->emulatorState.t_states + tState;

    bool tapeBit = getTapeStreamBit(clockCount);

    if (tapeBit != _lastTapeBit)
    {
        _lastTapeBit = tapeBit;

        int16_t amp = tapeBit ? 6000 : -6000;
        uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
        uint32_t scaledTState = tState * speedMultiplier;

        _context->pSoundManager->updateDAC(scaledTState, amp, amp);
    }
}

void Tape::handleFrameEnd()
{
    if (!_tapeStarted)
        return;

    // Check ERR_NR - ROM sets error code on break/error (immediate detection)
    // System variables are in RAM at same addresses regardless of which ROM is paged
    uint8_t errNr = _context->pMemory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
    if (errNr != _initialErrNr)
    {
        MLOGINFO("Tape stopped: ERR_NR changed from 0x%02X to 0x%02X", _initialErrNr, errNr);
        stopTape();
        return;
    }

    // Track frames since last tape read (backup detection for load complete)
    // 128K mode has longer gaps between reads due to ROM switching
    _framesSinceLastRead++;

    // 150 frames (~3 seconds) without reads = loader exited
    if (_framesSinceLastRead > 150)
    {
        stopTape();
    }
}

/// endregion </Emulation events>

/// region <Helper methods>

bool Tape::getTapeStreamBit(uint64_t clockCount)
{
    if (!_tapeStarted || _currentTapeBlockIndex == UINT64_MAX)
    {
        _currentClockCount = clockCount;
        return _tapeBitState;
    }

    if (_currentClockCount == 0 || clockCount <= _currentClockCount)
    {
        _currentClockCount = clockCount;
        return _tapeBitState;
    }

    uint64_t deltaTime = clockCount - _currentClockCount;
    _currentClockCount = clockCount;

    while (deltaTime > 0 && _currentTapeBlockIndex < _tapeBlocks.size())
    {
        if (_currentTapeBlock == nullptr)
        {
            _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];
            generateBitstreamForStandardBlock(*_currentTapeBlock);
            _currentOffsetWithinPulse = 0;
            _currentPulseIdxInBlock = 0;
        }

        TapeBlock& block = *_currentTapeBlock;
        if (_currentOffsetWithinPulse >= block.edgePulseTimings.size())
        {
            _currentTapeBlockIndex++;
            _currentTapeBlock = nullptr;
            _currentOffsetWithinPulse = 0;
            _currentPulseIdxInBlock = 0;

            if (_currentTapeBlockIndex >= _tapeBlocks.size())
            {
                stopTape();
                break;
            }
            continue;
        }

        uint32_t currentPulseDuration = block.edgePulseTimings[_currentOffsetWithinPulse];
        uint32_t remainingInPulse = (currentPulseDuration > _currentPulseIdxInBlock)
                                      ? (currentPulseDuration - static_cast<uint32_t>(_currentPulseIdxInBlock))
                                      : 0;

        if (deltaTime < remainingInPulse)
        {
            _currentPulseIdxInBlock += deltaTime;
            deltaTime = 0;
        }
        else
        {
            deltaTime -= remainingInPulse;
            _currentOffsetWithinPulse++;
            _currentPulseIdxInBlock = 0;
            _tapeBitState = !_tapeBitState;  // Flip digital tape bit on pulse edge transition!

            if (_currentOffsetWithinPulse >= block.edgePulseTimings.size())
            {
                _currentTapeBlockIndex++;
                _currentTapeBlock = nullptr;
                _currentOffsetWithinPulse = 0;
                _currentPulseIdxInBlock = 0;

                if (_currentTapeBlockIndex >= _tapeBlocks.size())
                {
                    stopTape();
                    break;
                }
            }
        }
    }

    return _tapeBitState;
}

/// Generate bitstream assistive data for the TapeBlock data
/// @param tapeBlock Reference to single TapeBlock object
/// @return Result whether generating process finished successfully or not
bool Tape::generateBitstreamForStandardBlock(TapeBlock& tapeBlock)
{
    bool result = false;

    bool isHeader = tapeBlock.type == TAP_BLOCK_FLAG_HEADER;

    size_t totalBlockDuration =
        generateBitstream(tapeBlock, PILOT_TONE_HALF_PERIOD, PILOT_SYNCHRO_1, PILOT_SYNCHRO_2, ZERO_ENCODE_HALF_PERIOD,
                          ONE_ENCODE_HALF_PERIOD, isHeader ? PILOT_DURATION_HEADER : PILOT_DURATION_DATA, 1000);

    if (totalBlockDuration > 0)
    {
        result = true;
    }

    return result;
}

size_t Tape::generateBitstream(TapeBlock& tapeBlock, uint16_t pilotHalfPeriod_tStates, uint16_t synchro1_tStates,
                               uint16_t synchro2_tStates, uint16_t zeroEncodingHalfPeriod_tState,
                               uint16_t oneEncodingHalfPeriod_tStates, size_t pilotLength_periods, size_t pause_ms)
{
    size_t result = 0;
    size_t len = tapeBlock.data.size();

    // Calculate collection size to fit all edge time intervals
    size_t resultSize = 0;
    resultSize += pilotLength_periods;        // Pilot length is specified in pulses (half-periods), one edge each
    resultSize += 2;                          // Two sync pulses at the end of pilot
    resultSize += (len * 8 * 2);              // Each byte split to bits and each bit encoded as 2 edges
    if (pause_ms > 0)
        resultSize += 1;  // Pause is just a marker so single edge is sufficient

    tapeBlock.edgePulseTimings.reserve(resultSize);

    /// region <Pilot tone + sync>

    if (pilotLength_periods > 0)
    {
        // Pilot length is specified in pulses (half-periods), matching the TAP
        // convention (header: 8063-8064 pulses, data: ~3220 pulses). Emitting
        // 2x here would double the real pilot duration (~10s instead of ~5s).
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

            // Each bit is encoded by two edges; count both so
            // totalBitstreamLength equals the sum of edgePulseTimings
            tapeBlock.edgePulseTimings.push_back(bitEncoded);
            tapeBlock.edgePulseTimings.push_back(bitEncoded);

            result += bitEncoded;
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
    [[maybe_unused]] static uint16_t counter = 0;
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