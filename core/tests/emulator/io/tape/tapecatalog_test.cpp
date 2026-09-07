#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tapecatalog.h"
#include "emulator/io/tape/tape.h"  // TapeCUT + ROM timing constants (_CODE_UNDER_TEST is target-wide)

/// region <Test fixture>

/// TapeCatalogParser is a pure function, but the duration-parity tests need
/// the real engine (generateBitstream) to compare against — hence the
/// emulator-backed fixture, mirroring Tape_Test.
class TapeCatalogParser_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TapeCUT* _tape = nullptr;

protected:
    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        if (!_emulator->Init())
        {
            throw std::runtime_error("Failed to initialize emulator for TapeCatalogParser_Test");
        }

        _context = _emulator->GetContext();
        _tape = new TapeCUT(_context);
    }

    void TearDown() override
    {
        if (_tape != nullptr)
        {
            delete _tape;
            _tape = nullptr;
        }

        if (_emulator != nullptr)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
            _emulator = nullptr;
        }

        _context = nullptr;  // Owned by _emulator, don't delete
    }

    /// Standard ROM header block: [flag $00][type][name:10][len:2][p1:2][p2:2][checksum]
    static TapeBlock MakeHeaderBlock(uint8_t type, const std::string& name, uint16_t length,
                                      uint16_t param1, uint16_t param2, bool validChecksum = true)
    {
        std::vector<uint8_t> data(19, 0x00);
        data[0] = 0x00;
        data[1] = type;
        for (size_t i = 0; i < 10 && i < name.size(); i++)
        {
            data[2 + i] = static_cast<uint8_t>(name[i]);
        }
        data[12] = length & 0xFF;
        data[13] = length >> 8;
        data[14] = param1 & 0xFF;
        data[15] = param1 >> 8;
        data[16] = param2 & 0xFF;
        data[17] = param2 >> 8;

        uint8_t parity = 0;
        for (size_t i = 0; i < 18; i++)
        {
            parity ^= data[i];
        }
        data[18] = validChecksum ? parity : static_cast<uint8_t>(parity ^ 0xFF);

        TapeBlock block;
        block.blockIndex = 0;
        block.type = TAP_BLOCK_FLAG_HEADER;
        block.data = std::move(data);
        return block;
    }

    /// $FF data block with a correct parity byte
    static TapeBlock MakeDataBlock(const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> data;
        data.push_back(0xFF);
        data.insert(data.end(), payload.begin(), payload.end());

        uint8_t parity = 0;
        for (uint8_t byte : data)
        {
            parity ^= byte;
        }
        data.push_back(parity);

        TapeBlock block;
        block.blockIndex = 0;
        block.type = TAP_BLOCK_FLAG_DATA;
        block.data = std::move(data);
        return block;
    }

    static TapeBlock MakeCustomBlock(uint8_t flag, const std::vector<uint8_t>& payload)
    {
        TapeBlock block;
        block.blockIndex = 0;
        block.type = static_cast<TapeBlockFlagEnum>(flag);
        block.data.push_back(flag);
        block.data.insert(block.data.end(), payload.begin(), payload.end());
        return block;
    }

    static TapeBlock MakePulseBlock(std::vector<uint32_t> pulses)
    {
        TapeBlock block;
        block.blockIndex = 0;
        block.type = TAP_BLOCK_FLAG_DATA;
        block.edgePulseTimings = std::move(pulses);
        return block;
    }

    static TapeImage MakeImage(std::vector<TapeBlock> blocks)
    {
        TapeImage image;
        for (size_t i = 0; i < blocks.size(); i++)
        {
            blocks[i].blockIndex = i;
        }
        image.blocks = std::move(blocks);
        return image;
    }
};

/// endregion </Test fixture>

/// region <Header interpretation and pairing>

TEST_F(TapeCatalogParser_Test, HeaderInterpretationAndPairing)
{
    TapeImage image = MakeImage({
        MakeHeaderBlock(TAP_BLOCK_PROGRAM, "PROG", 10, 1, 10),
        MakeDataBlock({ 0x11, 0x22, 0x33 }),
    });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);
    ASSERT_EQ(catalog.size(), 2u);

    // Header block
    EXPECT_EQ(catalog[0].kind, TapeBlockKindEnum::Header);
    EXPECT_EQ(catalog[0].name, "PROG");
    EXPECT_EQ(catalog[0].headerType, TAP_BLOCK_PROGRAM);
    EXPECT_EQ(catalog[0].declaredLength, 10u);
    EXPECT_EQ(catalog[0].param1, 1u);
    EXPECT_EQ(catalog[0].param2, 10u);
    EXPECT_TRUE(catalog[0].headerValid);
    EXPECT_TRUE(catalog[0].checksumValid);
    EXPECT_EQ(catalog[0].pairedDataIndex, 1u);
    EXPECT_FALSE(catalog[0].headerless);

    // Data block: paired, not headerless
    EXPECT_EQ(catalog[1].kind, TapeBlockKindEnum::Data);
    EXPECT_EQ(catalog[1].pairedHeaderIndex, 0u);
    EXPECT_FALSE(catalog[1].headerless);
    EXPECT_TRUE(catalog[1].checksumValid);
}

TEST_F(TapeCatalogParser_Test, TrailingSpacesTrimmedAndBit7Stripped)
{
    // 'N', 'A', 'M', 'E', 'S'|0x80, then trailing spaces
    TapeBlock block = MakeHeaderBlock(TAP_BLOCK_CODE, "NAME\xD3      ", 4, 0x6000, 0x80BF);

    TapeImage image = MakeImage({ std::move(block) });
    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].name, "NAMES");  // 0x8S & 0x7F = 'S'
}

TEST_F(TapeCatalogParser_Test, InvalidHeaderDoesNotPair)
{
    // $00 flag but only 10 bytes: not a 19-byte interpretable header
    TapeBlock malformed;
    malformed.blockIndex = 0;
    malformed.type = TAP_BLOCK_FLAG_HEADER;
    malformed.data = std::vector<uint8_t>(10, 0x00);

    TapeImage image = MakeImage({ std::move(malformed), MakeDataBlock({ 0xAA }) });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);
    ASSERT_EQ(catalog.size(), 2u);

    EXPECT_EQ(catalog[0].kind, TapeBlockKindEnum::Header);  // flag says header
    EXPECT_FALSE(catalog[0].headerValid);                   // ... but not interpretable
    EXPECT_TRUE(catalog[1].headerless) << "Data after an invalid header is headerless (the insult.tap distinction)";
    EXPECT_EQ(catalog[1].pairedHeaderIndex, static_cast<size_t>(SIZE_MAX));
}

TEST_F(TapeCatalogParser_Test, CustomFlagBlockIsHeaderless)
{
    TapeImage image = MakeImage({
        MakeCustomBlock(0x33, { 0xDE, 0xAD, 0xBE, 0xEF }),
    });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].kind, TapeBlockKindEnum::Custom);
    EXPECT_EQ(catalog[0].rawFlag, 0x33);
    EXPECT_TRUE(catalog[0].headerless);
}

TEST_F(TapeCatalogParser_Test, BadChecksumDetectedButStillPaired)
{
    // A header with a corrupted checksum is still an interpretable header —
    // checksum validity and header validity are independent judgments
    TapeImage image = MakeImage({
        MakeHeaderBlock(TAP_BLOCK_CODE, "CODE", 4, 0x6000, 0x8000, /*validChecksum=*/false),
        MakeDataBlock({ 0x55 }),
    });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_FALSE(catalog[0].checksumValid);
    EXPECT_TRUE(catalog[0].headerValid);
    EXPECT_EQ(catalog[1].pairedHeaderIndex, 0u);
    EXPECT_FALSE(catalog[1].headerless);
}

/// endregion </Header interpretation and pairing>

/// region <Timing, baud and duration>

TEST_F(TapeCatalogParser_Test, StandardRomProfileAndBaud)
{
    TapeImage image = MakeImage({
        MakeHeaderBlock(TAP_BLOCK_PROGRAM, "P", 1, 1, 1),
    });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].timing.profile, TapeSpeedProfileEnum::StandardRom);
    EXPECT_EQ(catalog[0].timing.pilotPulses, static_cast<uint32_t>(PILOT_DURATION_HEADER));
    EXPECT_EQ(catalog[0].baudEstimate, 3500000u / (855u + 1710u));
}

TEST_F(TapeCatalogParser_Test, DataBlockPilotPulses)
{
    TapeImage image = MakeImage({ MakeDataBlock({ 0x00 }) });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].timing.pilotPulses, static_cast<uint32_t>(PILOT_DURATION_DATA));
}

TEST_F(TapeCatalogParser_Test, DurationMatchesEngineForStandardRom)
{
    // Design §5.5: the duration model must use "the same formula as
    // generateBitstream — cannot disagree with playback"
    TapeBlock block = MakeHeaderBlock(TAP_BLOCK_PROGRAM, "PARITY", 3, 1, 3);

    ASSERT_TRUE(_tape->generateBitstreamForStandardBlock(block));

    TapeImage image = MakeImage({ std::move(block) });
    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_DOUBLE_EQ(catalog[0].estimatedSeconds,
                     static_cast<double>(image.blocks[0].totalBitstreamLength) / 3500000.0);
}

TEST_F(TapeCatalogParser_Test, DurationMatchesEngineForCustomProfile)
{
    // A TZX $11-shaped turbo profile: u32 fields (A1), distinct timings
    TapeTimingProfile profile;
    profile.profile = TapeSpeedProfileEnum::Custom;
    profile.pilotPulses = 100;
    profile.pilotHalfPeriod = 2000;
    profile.sync1 = 700;
    profile.sync2 = 800;
    profile.zeroHalfPeriod = 900;
    profile.oneHalfPeriod = 1800;
    profile.pauseMs = 500;

    TapeBlock block = MakeDataBlock({ 0xA5, 0x5A, 0x0F });
    block.timing = profile;

    size_t engineTotal = _tape->generateBitstream(block, 2000, 700, 800, 900, 1800, 100, 500);

    TapeImage image = MakeImage({ std::move(block) });
    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].timing.profile, TapeSpeedProfileEnum::Custom);
    EXPECT_EQ(catalog[0].baudEstimate, 3500000u / (900u + 1800u));
    EXPECT_DOUBLE_EQ(catalog[0].estimatedSeconds, static_cast<double>(engineTotal) / 3500000.0);
}

TEST_F(TapeCatalogParser_Test, PulseBlockDurationFromPulseSum)
{
    TapeImage image = MakeImage({ MakePulseBlock({ 1000, 2000, 3000 }) });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    EXPECT_EQ(catalog[0].timing.profile, TapeSpeedProfileEnum::PulseStream);
    EXPECT_EQ(catalog[0].baudEstimate, 0u);
    EXPECT_EQ(catalog[0].rawSize, 0u);
    EXPECT_DOUBLE_EQ(catalog[0].estimatedSeconds, 6000.0 / 3500000.0);
}

/// endregion </Timing, baud and duration>

/// region <Degenerate inputs>

TEST_F(TapeCatalogParser_Test, EmptyImageYieldsEmptyCatalog)
{
    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(TapeImage{});
    EXPECT_TRUE(catalog.empty());
}

TEST_F(TapeCatalogParser_Test, PayloadPreviewTruncatedToSixteenBytes)
{
    std::vector<uint8_t> payload(64, 0xEE);
    TapeImage image = MakeImage({ MakeDataBlock(payload) });

    std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);

    // 1 flag + 16 payload bytes in the preview — not the full 66-byte block
    EXPECT_EQ(catalog[0].payloadPreview.size(), 16u);
    EXPECT_EQ(catalog[0].rawSize, 66u);  // flag + 64 payload + parity
}

/// endregion </Degenerate inputs>
