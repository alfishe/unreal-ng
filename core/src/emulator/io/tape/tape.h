#pragma once

#include "stdafx.h"

#include "emulator/platform.h"


class EmulatorContext;
class ModuleLogger;

/// region <Constants>

constexpr uint16_t PILOT_TONE_HALF_PERIOD = 2168;       // Pilot tone has 2168 t-states half-period
constexpr uint16_t PILOT_SYNCHRO_1 = 667;               // At the end of pilot two synchro pulses are generated. First with 667 t-states duration
constexpr uint16_t PILOT_SYNCHRO_2 = 735;               //  second - with 735 t-states duration
constexpr uint16_t PILOT_DURATION_HEADER = 8064;        // Pilot for header block lasts for 3220 full period cycles (8064 * 2168 * 2)
constexpr uint16_t PILOT_DURATION_DATA = 3220;          // Pilot for data block lasts for 3220 full period cycles (3220 * 2168 * 2)
constexpr uint16_t ZERO_ENCODE_HALF_PERIOD = 855;       // Zeroes encoded as two 855 t-states half-periods
constexpr uint16_t ONE_ENCODE_HALF_PERIOD = 1710;       // One encoded as two 1710 t-states half-periods
constexpr uint16_t TAPE_PAUSE_BETWEEN_BLOCKS = 1000;    // 1000ms

/// endregion </Constants>

/// region <Types>

enum ZXTapeBlockTypeEnum : uint8_t
{
    TAP_BLOCK_PROGRAM = 0,          // Block contains BASIC program
    TAP_BLOCK_NUM_ARRAY,            // Block contains numeric array
    TAP_BLOCK_CHAR_ARRAY,           // Block contains symbolic array
    TAP_BLOCK_CODE                  // Block contains code
};

inline const char* getTapeBlockTypeName(ZXTapeBlockTypeEnum value)
{
    static const char* names[] =
    {
    "Program",
    "Numeric array",
    "Symbolic array",
    "Code"
    };

    return names[value];
};

enum TapeBlockFlagEnum : uint8_t
{
    TAP_BLOCK_FLAG_HEADER = 0x00,
    TAP_BLOCK_FLAG_DATA = 0xFF
};

inline const char* getTapeBlockFlagEnumName(TapeBlockFlagEnum value)
{
    const char* header = "Header";
    const char* data = "Data";
    const char* unknown = "<Unknown value";

    const char* result;
    switch (value)
    {
        case 0x00:
            result = header;
            break;
        case 0xFF:
            result = data;
            break;
        default:
            result = unknown;
            break;
    }

    return result;
};

struct TapeBlock
{
    // ID of the block itself
    size_t blockIndex;

    TapeBlockFlagEnum type;                 // Header or data block

    std::vector<uint8_t> data;              // Raw data

    size_t totalBitstreamLength = 0;        // How long in t-states current block will be played
    std::vector<uint32_t> edgePulseTimings; // Block data encoded to pulse edge series
};

/// endregion <Types>

/// A 'pulse' here is either a mark or a space, so 2 pulses makes a complete square wave cycle.
/// Pilot tone: before each block is a sequence of 8063 (header) or 3223 (data) pulses, each of length 2168 T-states.
/// Sync pulses: the pilot tone is followed by two sync pulses of 667 and 735 T-states respectively
/// A '0' bit is encoded as 2 pulses of 855 T-states each.
/// A '1' bit is encoded as 2 pulses of 1710 T-states each (ie. twice the length of a '0')
///
/// The initial polarity of the signal does not matter - everything in the ROM loader is edge-triggered rather than level-triggered.
/// @see https://sinclair.wiki.zxnet.co.uk/wiki/Spectrum_tape_interface

/// Tape signal is frequency-modulation encoded
/// Signal types:
/// 1. Pilot tone - 807Hz (2168 high + 2168 low Z80 t-states @3.5MHz). Pilot Freq = 3500000 / (2168 + 2168) = 808Hz
///    Pilot tone duration:
///       - 8063 periods () - for the header
///       - 3223 periods () - for data block
/// 2. Synchronization signal - asymmetrical: 667 t-states high (190.6 uS) and 735 t-states low (210 uS)
/// 3. Data: 0-encoding - 2047Hz (855 high + 855 low t-states). Zero encoding Freq = 3500000 / (855 + 855) = 2047Hz
/// 4. Data: 1-encoding - 1023Hz (1710 high + 1710 low t-states). One encoding Freq = 3500000 / (1710 + 1710) = 1023Hz
///
/// The cassette loading routines have a great tolerance, and will allow variations in the speed of up to +/-15%
/// @see https://retrocomputing.stackexchange.com/questions/15810/zx-spectrum-red-stripes-during-loading
class Tape
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_TAPE;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;

    bool _tapeStarted;
    size_t _tapePosition;

    // Tape input bitstream related
    std::vector<TapeBlock> _tapeBlocks; // Tape representation as parsed TapeBlock vector

    TapeBlock* _currentTapeBlock;       // Shortcut to current block object
    size_t _currentTapeBlockIndex;      // Index of current TapeBlock
    size_t _currentPulseIdxInBlock;     // Index in TapeBlock::edgePulseTimings vector
    size_t _currentOffsetWithinPulse;   // How many pulses already processed within single TapeBlock::edgePulseTimings vector element
    size_t _currentClockCount;          // Store clock count for next iteration

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Tape() = delete;    // Disable default constructor. C++ 11 feature
    Tape(EmulatorContext* context);
    virtual ~Tape() = default;
    /// endregion </Constructors / Destructors>

    /// region <Tape control methods>
public:
    void reset();
    void startTape();
    void stopTape();
    /// endregion </Tape control methods>

    /// region <Port events>
public:
    uint8_t handlePortIn();
    void handlePortOut(bool value);
    /// endregion </Port events>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleFrameEnd();
    /// endregion </Emulation events>

    /// region <Helper methods>
protected:
    bool getTapeStreamBit(uint64_t clockCount);

    bool generateBitstreamForStandardBlock(TapeBlock& tapeBlock);

    size_t generateBitstream(TapeBlock& tapeBlock,
                             uint16_t pilotHalfPeriod_tStates,
                             uint16_t synchro1_tStates,
                             uint16_t synchro2_tStates,
                             uint16_t zeroEncodingHalfPeriod_tState,
                             uint16_t oneEncodingHalfPeriod_tStates,
                             size_t pilotLength_periods,
                             size_t pause_ms);

    // FIXME: just experimentation method
    bool getPilotSample(size_t clockCount);

    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class TapeCUT : public Tape
{
public:
    TapeCUT(EmulatorContext* context) : Tape(context) {};

    using Tape::handlePortIn;
    using Tape::generateBitstream;

    using Tape::getPilotSample;
};

#endif // _CODE_UNDER_TEST