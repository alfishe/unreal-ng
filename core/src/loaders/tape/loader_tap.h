#pragma once

#include "stdafx.h"
#include "emulator/io/tape/tapetypes.h"
#include "loaders/tape/loader_tape.h"

/// region <Documentation>

/// region <TAP format>
// @see TAP format: https://faqwiki.zxnet.co.uk/wiki/TAP_format
// @see: https://k1.spdns.de/Develop/Projects/zasm/Info/tap.txt
// @see: https://documentation.help/BASin/format_tape.html
// @see: http://web.archive.org/web/20110711141601/http://www.zxmodules.de/fileformats/tapformat.html
// @see: https://shred.zone/cilla/page/440/r-tape-loading-error.html
// @see: https://shred.zone/cilla/page/441/r-tape-loading-error-part-2.html
// @see TAP structures: https://formats.kaitai.io/zx_spectrum_tap/index.html

//    The .TAP files contain blocks of tape-saved data.
//    All blocks start with two paramBytes specifying how many paramBytes will follow (not counting the two length paramBytes).
//    Then raw tape data follows, including the flag and checksum paramBytes. The checksum is the bitwise XOR of all paramBytes
//    including the flag byte.
//
//    For example, when you execute the line SAVE "ROM" CODE 0,2 this will result:
//          |------ Spectrum-generated data -------|       |---------|
//
//    13 00 00 03 52 4f 4d 7x20 02 00 00 00 00 80 f1 04 00 ff f3 af a3
//
//    ^^_^^...... first block is 19 paramBytes (17 paramBytes+flag+checksum)
//          ^^... flag byte (A reg, 00 for headers, ff for data blocks)
//             ^^ first byte of header, indicating a code block
//
//    file name ..^^^^^^^^^^^^^
//    header info ..............^^^^^^^^^^^^^^^^^
//    checksum of header .........................^^
//    length of second block ........................^^_^^
//    flag byte ...........................................^^
//    first two paramBytes of rom .................................^^_^^
//    checksum ......................................................^^
/// endregion </TAP format>

/// region <ZX-Spectrum tape timings>

/// Tape signal is frequency-modulation encoded
/// Signal types:
/// 1. Pilot tone - 807Hz (2168 high + 2168 low Z80 t-states @3.5MHz). Pilot Freq = 3500000 / (2168 + 2168) = 887Hz
/// 2. Synchronization signal - asymmetrical: 668 t-states high (190.6 uS) and 735 t-states low (210 uS)
/// 3. Data: 0-encoding - 2047Hz (855 high + 855 low t-states). Zero Freq = 3500000 / (855 + 855) = 2047Hz
/// 4. Data: 1-encoding - 1023Hz (1710 high + 1710 low t-states). One Freq = 3500000 / (1710 + 1710) = 1023Hz

/// endregion </ZX-Spectrum tape timings>

/// endregion </Documentation>

class EmulatorContext;
class ModuleLogger;

class LoaderTAP : public LoaderTapeBase
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    /// Context-free construction for the loader registry (design §5.3): the
    /// contract is pure data in, TapeImage out — anomalies travel through
    /// TapeImage::status / parseWarnings, not a logger.
    LoaderTAP();

    /// Legacy context-bound construction (existing tests and CUT wrapper).
    LoaderTAP(EmulatorContext* context);
    virtual ~LoaderTAP();
    /// endregion </Constructors / destructors>

    /// region <LoaderTapeBase contract>
public:
    TapeImage Load(std::span<const uint8_t> bytes, const std::string& sourceName) override;
    const TapeFormatInfo& Format() const override;
    /// endregion </LoaderTapeBase contract>

    /// region <Methods>
public:
    /// Structural content probe (design §5.3): walks the [u16 len] framing
    /// to EOF. Clean landing = 100, at least one complete block before an
    /// overrun = 25, nothing complete = 0. Doubles as the Warajevo `.tap`
    /// discriminator (different framing behind the same extension).
    static int Probe(std::span<const uint8_t> bytes);

    std::vector<TapeBlock> loadTAP(std::string filePath);
    /// endregion </Methods>

protected:
    TapeBlock readNextBlock(FILE* file);

    bool isBlockValid(const vector<uint8_t>& blockData);
    uint8_t getBlockChecksum(const vector<uint8_t>& blockData);
    void convertPayloadDataToBitstream(const vector<uint8_t>& payloadData);

    /// region <Debug methods>

public:
    std::string dumpBlocks(const std::vector<TapeBlock>& dataBlocks);
    std::string dumpBlock(const std::vector<uint8_t>& dataBlock);

    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderTAPCUT : public LoaderTAP
{
public:
    LoaderTAPCUT(EmulatorContext* context) : LoaderTAP(context) {};

public:
    using LoaderTAP::_context;
    using LoaderTAP::_logger;

    using LoaderTAP::readNextBlock;
    using LoaderTAP::isBlockValid;
    using LoaderTAP::getBlockChecksum;
};
#endif // _CODE_UNDER_TEST
