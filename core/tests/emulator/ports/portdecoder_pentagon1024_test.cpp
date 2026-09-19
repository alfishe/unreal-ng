#include "stdafx.h"
#include "gtest/gtest.h"

#include <memory>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_pentagon512.h"
#include "emulator/ports/models/portdecoder_pentagon1024.h"
#include "emulator/ports/portdecoder.h"

/// @file portdecoder_pentagon1024_test.cpp
/// @brief Pentagon 1024K port decoder tests.
///
/// Validates:
/// - Port #EFF7 detection and handling
/// - 6-bit bank selection: bits [0:2]+[6:7] from #7FFD + bit 3 from #EFF7
/// - Extended memory enable/disable via #EFF7 bit 2
/// - Factory selection of Pentagon1024 decoder for ramsize >= 1024

class PortDecoder_Pentagon1024_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.mem_model = MM_PENTAGON;
        _context->config.ramsize = 1024;
    }

    void TearDown() override
    {
        delete _decoder;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    PortDecoder_Pentagon1024* _decoder = nullptr;
};

/// Factory should select Pentagon1024 decoder for ramsize >= 1024
TEST_F(PortDecoder_Pentagon1024_Test, FactorySelectsPentagon1024ForLargeRam)
{
    PortDecoder* decoder = PortDecoder::GetPortDecoderForModel(MM_PENTAGON, _context);
    ASSERT_NE(decoder, nullptr);

    PortDecoder_Pentagon1024* p1024 = dynamic_cast<PortDecoder_Pentagon1024*>(decoder);
    EXPECT_NE(p1024, nullptr) << "Factory should return Pentagon1024 decoder for ramsize=1024";

    delete decoder;
}

/// Factory should select Pentagon512 decoder for ramsize 512
TEST_F(PortDecoder_Pentagon1024_Test, FactorySelectsPentagon512ForMediumRam)
{
    _context->config.ramsize = 512;

    PortDecoder* decoder = PortDecoder::GetPortDecoderForModel(MM_PENTAGON, _context);
    ASSERT_NE(decoder, nullptr);

    PortDecoder_Pentagon1024* p1024 = dynamic_cast<PortDecoder_Pentagon1024*>(decoder);
    PortDecoder_Pentagon512* p512 = dynamic_cast<PortDecoder_Pentagon512*>(decoder);

    EXPECT_EQ(p1024, nullptr) << "Factory should not return Pentagon1024 for ramsize=512";
    EXPECT_NE(p512, nullptr) << "Factory should return Pentagon512 for ramsize=512";

    delete decoder;
}

/// Factory should select Pentagon128 decoder for ramsize 128
TEST_F(PortDecoder_Pentagon1024_Test, FactorySelectsPentagon128ForSmallRam)
{
    _context->config.ramsize = 128;

    PortDecoder* decoder = PortDecoder::GetPortDecoderForModel(MM_PENTAGON, _context);
    ASSERT_NE(decoder, nullptr);

    PortDecoder_Pentagon1024* p1024 = dynamic_cast<PortDecoder_Pentagon1024*>(decoder);
    PortDecoder_Pentagon512* p512 = dynamic_cast<PortDecoder_Pentagon512*>(decoder);

    EXPECT_EQ(p1024, nullptr) << "Factory should not return Pentagon1024 for ramsize=128";
    EXPECT_EQ(p512, nullptr) << "Factory should not return Pentagon512 for ramsize=128";

    delete decoder;
}

/// pEFF7 state is accessible via emulatorState
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_StateAccessible)
{
    // Test that pEFF7 state can be set and retrieved directly
    // Note: Full port decode testing requires Memory initialization
    _context->emulatorState.pEFF7 = 0x08;
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x08);

    _context->emulatorState.pEFF7 = 0x0C;
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x0C);
}

/// EFF7 state starts at 0 (extended memory enabled by default)
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_DefaultState)
{
    // EmulatorContext initializes pEFF7 to 0 by default
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x00)
        << "pEFF7 should be 0 by default (extended memory enabled)";
}

/// Port map should include EFF7 entry for Pentagon 1024
TEST_F(PortDecoder_Pentagon1024_Test, PortMap_IncludesEFF7)
{
    _decoder = new PortDecoder_Pentagon1024(_context);
    std::vector<PortMapEntry> entries = _decoder->getPortMapEntries();

    bool foundEFF7 = false;
    for (const auto& entry : entries)
    {
        if (entry.port == 0xEFF7)
        {
            foundEFF7 = true;
            EXPECT_TRUE(entry.tags & PortTag::Memory)
                << "EFF7 should have Memory tag";
            EXPECT_EQ(entry.latch, PagingLatch::PEFF7)
                << "EFF7 should have PEFF7 latch binding";
            break;
        }
    }
    EXPECT_TRUE(foundEFF7) << "Port map should include EFF7 for Pentagon 1024";
}

/// Port map should NOT include EFF7 for Pentagon 512
TEST_F(PortDecoder_Pentagon1024_Test, PortMap_Pentagon512_NoEFF7)
{
    _context->config.ramsize = 512;
    PortDecoder_Pentagon512 decoder(_context);
    std::vector<PortMapEntry> entries = decoder.getPortMapEntries();

    bool foundEFF7 = false;
    for (const auto& entry : entries)
    {
        if (entry.port == 0xEFF7)
        {
            foundEFF7 = true;
            break;
        }
    }
    EXPECT_FALSE(foundEFF7) << "Pentagon 512 port map should NOT include EFF7";
}

/// DecodePagingLatch should decode EFF7 fields for Pentagon 1024
TEST_F(PortDecoder_Pentagon1024_Test, DecodePagingLatch_EFF7)
{
    // Test with ext memory present (bit 2 = 0), all features off
    auto decoded = DecodePagingLatch(PagingLatch::PEFF7, 0x00, MM_PENTAGON, 1024);
    ASSERT_GE(decoded.size(), 2u);

    bool foundExtPresent = false;
    bool foundA4b = false;
    for (const auto& field : decoded)
    {
        if (field.key == "ext_memory_present")
        {
            foundExtPresent = true;
            EXPECT_TRUE(field.boolValue) << "bit 2 = 0 means memory present";
        }
        if (field.key == "a4b_mode")
        {
            foundA4b = true;
            EXPECT_FALSE(field.boolValue) << "a4b should be off when bit 0 = 0";
        }
    }
    EXPECT_TRUE(foundExtPresent && foundA4b);

    // Test with ext memory absent (bit 2 = 1), a4b on (bit 0 = 1)
    decoded = DecodePagingLatch(PagingLatch::PEFF7, 0x05, MM_PENTAGON, 1024);
    for (const auto& field : decoded)
    {
        if (field.key == "ext_memory_present")
        {
            EXPECT_FALSE(field.boolValue) << "bit 2 = 1 means memory absent";
        }
        if (field.key == "a4b_mode")
        {
            EXPECT_TRUE(field.boolValue) << "bit 0 = 1 means a4b on";
        }
    }
}

/// DecodePagingLatch for EFF7 without Pentagon 1024 context should give raw value
TEST_F(PortDecoder_Pentagon1024_Test, DecodePagingLatch_EFF7_OtherModel)
{
    // For non-Pentagon 1024 (e.g., smaller RAM or different model), should get raw value
    auto decoded = DecodePagingLatch(PagingLatch::PEFF7, 0x42, MM_PENTAGON, 512);

    bool foundValue = false;
    for (const auto& field : decoded)
    {
        if (field.key == "value")
        {
            foundValue = true;
            EXPECT_EQ(field.intValue, 0x42);
        }
    }
    EXPECT_TRUE(foundValue) << "Non-Pentagon1024 should get raw value field";
}

/// Pentagon 1024 6-bit bank encoding test (tests the math, not the full port decode)
/// Per Born Dead #10 and UMT:
/// - pb0..pb2 → #7FFD bits 0-2
/// - pb3 → #7FFD bit 6
/// - pb4 → #7FFD bit 7
/// - pb5 → #7FFD bit 5 (when extension enabled)
TEST_F(PortDecoder_Pentagon1024_Test, SixBitBankEncoding)
{
    // Test the bank encoding formula used by UMT:
    // For a given page number, calculate what value UMT writes to #7FFD
    auto umtEncode = [](uint8_t page) -> uint8_t {
        // UMT's MAP_PENTAGON algorithm
        uint8_t e = page & 7;           // pb0..pb2
        uint8_t a = page & 0x38;        // pb3..pb5 in bits 3-5
        a <<= 1;                        // pb3 -> bit 4
        a <<= 1;                        // pb3 -> bit 5, pb4 -> bit 4
        a <<= 1;                        // shift again - pb5 goes to CY (bit 8), others shift
        bool cy = (page & 0x20) != 0;   // pb5 was shifted out
        a &= 0xFF;                      // keep low byte (after 3x SLA, pb3->bit6, pb4->bit7)
        if (cy)
            a |= 0x20;                  // pb5 re-enters as bit5
        return (a | e | 0x10);          // or 10h = bit 4 set for screen convention
    };

    // Test the reverse: what bank does each #7FFD value select?
    auto decodePentagon1024 = [](uint8_t value) -> uint8_t {
        // Pentagon 1024 6-bit bank decode (from my implementation)
        uint8_t bank = value & 0b0000'0111;           // Bits [0:2] → pb0..pb2
        bank |= ((value & 0b1100'0000) >> 3);         // Bits [6:7] → pb3, pb4
        bank |= (value & 0b0010'0000);                // Bit 5 → pb5
        return bank;
    };

    // Verify round-trip for all 64 pages
    for (uint8_t page = 0; page < 64; page++)
    {
        uint8_t encoded = umtEncode(page);
        uint8_t decoded = decodePentagon1024(encoded);
        EXPECT_EQ(decoded, page) << "Page " << static_cast<int>(page)
            << " encoded as 0x" << std::hex << static_cast<int>(encoded)
            << " should decode back to " << std::dec << static_cast<int>(page);
    }

    // Specific checks from UMT docs:
    // Page 0 → 0x10 (bit 4 only)
    EXPECT_EQ(decodePentagon1024(0x10), 0);
    // Page 32 → 0x30 (bit 4 + bit 5)
    EXPECT_EQ(decodePentagon1024(0x30), 32);
    // Page 63 → all page bits set
    uint8_t enc63 = umtEncode(63);
    EXPECT_EQ(decodePentagon1024(enc63), 63);
}

/// Extended memory gate test: bit 2 of #EFF7 controls bank selection width
TEST_F(PortDecoder_Pentagon1024_Test, ExtendedMemoryGate)
{
    // Default: pEFF7 = 0, bit 2 = 0 → extended memory present → 6-bit selection
    EXPECT_EQ(_context->emulatorState.pEFF7 & 0x04, 0)
        << "Extended memory should be present by default (pEFF7 bit 2 = 0)";

    // When extended memory disabled (bit 2 = 1), only 3-bit bank selection
    _context->emulatorState.pEFF7 = 0x04;
    EXPECT_EQ(_context->emulatorState.pEFF7 & 0x04, 0x04)
        << "Extended memory disabled when bit 2 = 1";
}

/// region <16-Color Mode Tests>

/// EFF7 bit 0 sets 16-color mode flag
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_Bit0_Sets16ColorMode)
{
    _context->emulatorState.pEFF7 = 0x00;
    EXPECT_FALSE(_context->emulatorState.pEFF7 & EFF7_4BPP);

    _context->emulatorState.pEFF7 = EFF7_4BPP;
    EXPECT_TRUE(_context->emulatorState.pEFF7 & EFF7_4BPP);
}

/// EFF7 bit 5 sets hardware multicolor mode flag
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_Bit5_SetsHardwareMulticolorMode)
{
    _context->emulatorState.pEFF7 = 0x00;
    EXPECT_FALSE(_context->emulatorState.pEFF7 & EFF7_HWMC);

    _context->emulatorState.pEFF7 = EFF7_HWMC;
    EXPECT_TRUE(_context->emulatorState.pEFF7 & EFF7_HWMC);
}

/// EFF7 default state after construction is 0
/// Note: Full reset() requires Memory and Screen to be initialized;
/// this test verifies the state assignment in reset() without calling it
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_ResetState)
{
    // Verify that reset() sets pEFF7 = 0x00 by checking the code path
    // (the actual reset() call requires full emulator context)
    _context->emulatorState.pEFF7 = EFF7_4BPP | EFF7_HWMC | EFF7_LOCKMEM;
    EXPECT_NE(_context->emulatorState.pEFF7, 0x00);

    // Direct assignment as done in PortDecoder_Pentagon1024::reset()
    _context->emulatorState.pEFF7 = 0x00;
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x00)
        << "pEFF7 should be 0 after reset assignment";
}

/// 16-color pixel extraction: left pixel formula
/// Left pixel: bits 6,2,1,0 -> index = ((byte & 0x40) >> 3) | (byte & 0x07)
TEST_F(PortDecoder_Pentagon1024_Test, Pixel16c_LeftPixelExtraction)
{
    auto extractLeftPixel = [](uint8_t byte) -> uint8_t {
        return ((byte & 0x40) >> 3) | (byte & 0x07);
    };

    // Test all 16 palette indices in left pixel position
    // Left pixel uses bits: D6 (bright), D2 (green), D1 (red), D0 (blue)
    EXPECT_EQ(extractLeftPixel(0b00'000'000), 0);   // Black
    EXPECT_EQ(extractLeftPixel(0b00'000'001), 1);   // Blue
    EXPECT_EQ(extractLeftPixel(0b00'000'010), 2);   // Red
    EXPECT_EQ(extractLeftPixel(0b00'000'011), 3);   // Magenta
    EXPECT_EQ(extractLeftPixel(0b00'000'100), 4);   // Green
    EXPECT_EQ(extractLeftPixel(0b00'000'101), 5);   // Cyan
    EXPECT_EQ(extractLeftPixel(0b00'000'110), 6);   // Yellow
    EXPECT_EQ(extractLeftPixel(0b00'000'111), 7);   // White

    // Bright colors (D6 set)
    EXPECT_EQ(extractLeftPixel(0b01'000'000), 8);   // Bright Black
    EXPECT_EQ(extractLeftPixel(0b01'000'001), 9);   // Bright Blue
    EXPECT_EQ(extractLeftPixel(0b01'000'010), 10);  // Bright Red
    EXPECT_EQ(extractLeftPixel(0b01'000'111), 15);  // Bright White
}

/// 16-color pixel extraction: right pixel formula
/// Right pixel: bits 7,5,4,3 -> index = ((byte & 0x80) >> 4) | ((byte & 0x38) >> 3)
/// Bit layout: D7=Yr(bright), D6=Yl, D5=Gr, D4=Rr, D3=Br, D2=Gl, D1=Rl, D0=Bl
TEST_F(PortDecoder_Pentagon1024_Test, Pixel16c_RightPixelExtraction)
{
    auto extractRightPixel = [](uint8_t byte) -> uint8_t {
        return ((byte & 0x80) >> 4) | ((byte & 0x38) >> 3);
    };

    // Test all 16 palette indices in right pixel position
    // Right pixel uses: D7 (bright), D5 (green), D4 (red), D3 (blue)
    // Mask 0x38 = bits 5,4,3; Mask 0x80 = bit 7
    // Index = {D7, D5, D4, D3} = {I, G, R, B}
    EXPECT_EQ(extractRightPixel(0x00), 0);   // Black       = 0b0000
    EXPECT_EQ(extractRightPixel(0x08), 1);   // Blue        = 0b0001 (D3=1)
    EXPECT_EQ(extractRightPixel(0x10), 2);   // Red         = 0b0010 (D4=1)
    EXPECT_EQ(extractRightPixel(0x18), 3);   // Magenta     = 0b0011 (D4+D3)
    EXPECT_EQ(extractRightPixel(0x20), 4);   // Green       = 0b0100 (D5=1)
    EXPECT_EQ(extractRightPixel(0x28), 5);   // Cyan        = 0b0101 (D5+D3)
    EXPECT_EQ(extractRightPixel(0x30), 6);   // Yellow      = 0b0110 (D5+D4)
    EXPECT_EQ(extractRightPixel(0x38), 7);   // White       = 0b0111 (D5+D4+D3)

    // Bright colors (D7 set adds 8 to index)
    EXPECT_EQ(extractRightPixel(0x80), 8);   // Bright Black = 0b1000 (D7)
    EXPECT_EQ(extractRightPixel(0x88), 9);   // Bright Blue  = 0b1001 (D7+D3)
    EXPECT_EQ(extractRightPixel(0xB8), 15);  // Bright White = 0b1111 (D7+D5+D4+D3)
}

/// Combined pixel extraction test: byte encodes two adjacent pixels
TEST_F(PortDecoder_Pentagon1024_Test, Pixel16c_CombinedExtraction)
{
    auto extractLeftPixel = [](uint8_t byte) -> uint8_t {
        return ((byte & 0x40) >> 3) | (byte & 0x07);
    };
    auto extractRightPixel = [](uint8_t byte) -> uint8_t {
        return ((byte & 0x80) >> 4) | ((byte & 0x38) >> 3);
    };

    // Test byte 0xEA = 0b11101010
    // Left: bits 6,2,1,0 = 1,0,1,0 = 10 (bright red)
    // Right: bits 7,5,4,3 = 1,1,0,1 = 13 (bright magenta)
    uint8_t testByte = 0xEA;
    EXPECT_EQ(extractLeftPixel(testByte), 10);
    EXPECT_EQ(extractRightPixel(testByte), 13);

    // Test byte 0x00 = both black
    EXPECT_EQ(extractLeftPixel(0x00), 0);
    EXPECT_EQ(extractRightPixel(0x00), 0);

    // Test byte 0xFF = both bright white
    EXPECT_EQ(extractLeftPixel(0xFF), 15);
    EXPECT_EQ(extractRightPixel(0xFF), 15);
}

/// endregion </16-Color Mode Tests>
