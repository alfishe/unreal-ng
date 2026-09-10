#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"

/// @file Port address decoding for the simple model decoders.
///
/// Each of these decoders answers a single question per port line: "does this
/// 16-bit port select register X?". Hardware expresses that as a GAL equation
/// over a handful of address lines, which is exactly a (mask, match) pair. Every
/// test below therefore has the same shape - sweep all 65536 ports and require
/// the decoder to agree with the reference equation on every one of them - so
/// the sweep lives in one helper instead of being copy-pasted per model.
///
/// Consolidated from portdecoder_{spectrum128,spectrum3,profi,scorpion256}_test.
/// {cpp,h}. Suite and test names are unchanged, so existing --gtest_filter
/// expressions still select exactly the same cases.

namespace
{
/// @brief Reference decode equation: port selects the register iff (port & mask) == match.
struct DecodeEquation
{
    uint16_t mask;
    uint16_t match;
};

/// @brief Assert a decoder predicate agrees with its reference equation on every port.
///
/// The comparison is a plain branch rather than ASSERT_EQ per iteration: this
/// runs 65536 times per test, and only the failing port is worth reporting.
template <typename Predicate>
void ExpectDecodeMatchesEquation(const char* portName, Predicate isPort, DecodeEquation equation)
{
    for (uint32_t i = 0; i <= 0xFFFF; i++)
    {
        const uint16_t port = static_cast<uint16_t>(i);
        const bool expected = (port & equation.mask) == equation.match;
        const bool actual = isPort(port);

        if (expected != actual)
        {
            char message[256];
            snprintf(message, sizeof message, "%s: port #%04X. Expected: %d, returned: %d", portName, port,
                     expected, actual);
            FAIL() << message;
        }
    }
}

/// @brief Common context + decoder lifecycle shared by every model fixture.
template <typename DecoderT>
class PortDecoderModelFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    DecoderT* _portDecoder = nullptr;

    void SetUp() override
    {
        // Instantiate emulator with all peripherals, but no configuration loaded
        _context = new EmulatorContext(LoggerLevel::LogError);
        _portDecoder = new DecoderT(_context);
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;

        delete _context;
        _context = nullptr;
    }
};
}  // namespace

/// region <Fixtures - names preserved from the per-model files>

class PortDecoder_Spectrum128_Test : public PortDecoderModelFixture<PortDecoder_Spectrum128>
{
};
class PortDecoder_Spectrum3_Test : public PortDecoderModelFixture<PortDecoder_Spectrum3>
{
};
class PortDecoder_Profi_Test : public PortDecoderModelFixture<PortDecoder_Profi>
{
};
class PortDecoder_Scorpion256_Test : public PortDecoderModelFixture<PortDecoder_Scorpion256>
{
};

/// endregion </Fixtures>

/// region <ZX-Spectrum 128k>

// Port #7FFD - sensitivity 0xxxxxxx xxxxx10x; equation /IORQ /WR /A15 A2 /A1.
// A2=1 is required to avoid clashing with the SOUNDRIVE ports (#F1/#F9 have A2=0).
TEST_F(PortDecoder_Spectrum128_Test, IsPort_7FFD)
{
    ExpectDecodeMatchesEquation(
        "#7FFD", [this](uint16_t port) { return _portDecoder->IsPort_7FFD(port); }, {0b1000'0000'0000'0110, 0b0000'0000'0000'0100});
}

/// endregion </ZX-Spectrum 128k>

/// region <ZX-Spectrum +3>

// Port #7FFD - equation /IORQ /WR /A15 A14 /A1
TEST_F(PortDecoder_Spectrum3_Test, IsPort_7FFD)
{
    ExpectDecodeMatchesEquation(
        "#7FFD", [this](uint16_t port) { return _portDecoder->IsPort_7FFD(port); }, {0b1100'0000'0000'0010, 0b0100'0000'0000'0000});
}

// Port #1FFD - equation /IORQ /WR /A15 /A14 /A13 A12 /A1
TEST_F(PortDecoder_Spectrum3_Test, IsPort_1FFD)
{
    ExpectDecodeMatchesEquation(
        "#1FFD", [this](uint16_t port) { return _portDecoder->IsPort_1FFD(port); }, {0b1111'0000'0000'0010, 0b0001'0000'0000'0000});
}

/// endregion </ZX-Spectrum +3>

/// region <Profi>

// Port #7FFD - equation /IORQ /WR /A15 A2 /A1 (as ZX-Spectrum 128k)
TEST_F(PortDecoder_Profi_Test, IsPort_7FFD)
{
    ExpectDecodeMatchesEquation(
        "#7FFD", [this](uint16_t port) { return _portDecoder->IsPort_7FFD(port); }, {0b1000'0000'0000'0110, 0b0000'0000'0000'0100});
}

// Port #DFFD - sensitivity xx0xxxxx xxxxxx0x; equation /IORQ /WR /A13 /A1
TEST_F(PortDecoder_Profi_Test, IsPort_DFFD)
{
    ExpectDecodeMatchesEquation(
        "#DFFD", [this](uint16_t port) { return _portDecoder->IsPort_DFFD(port); }, {0b0010'0000'0000'0010, 0b0000'0000'0000'0000});
}

/// endregion </Profi>

/// region <Scorpion ZS-256>

// Port #7FFD - equation /IORQ /WR M1 /A15 A14 A12 A5 A2 /A1 A0
TEST_F(PortDecoder_Scorpion256_Test, IsPort_7FFD)
{
    ExpectDecodeMatchesEquation(
        "#7FFD", [this](uint16_t port) { return _portDecoder->IsPort_7FFD(port); }, {0b1101'0000'0010'0111, 0b0101'0000'0010'0101});
}

// Port #1FFD - equation /IORQ /WR M1 /A15 /A14 A12 A5 A2 /A1 A0
TEST_F(PortDecoder_Scorpion256_Test, IsPort_1FFD)
{
    ExpectDecodeMatchesEquation(
        "#1FFD", [this](uint16_t port) { return _portDecoder->IsPort_1FFD(port); }, {0b1101'0000'0010'0111, 0b0001'0000'0010'0101});
}

/// endregion </Scorpion ZS-256>
