// The EAR bit (#FE bit 6) with no tape playing, per board (Tape::handlePortIn, "Idle EAR level").
// z80test's hardware CRCs of the IN tests need the Spectrum level; the Scorpion's ProfROM needs HIGH.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

#include <string>

namespace
{

constexpr uint16_t kPortFE = 0xFFFE;  // no keyboard half-row selected
constexpr uint8_t kEar = 0x40;

class TapeIdleEar_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    PortDecoder* Create(const std::string& model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        if (!_emulator || !_emulator->GetContext())
            return nullptr;
        return _emulator->GetContext()->pPortDecoder;
    }

    Emulator* _emulator = nullptr;
};

}  // namespace

// Ferranti ULA, issue 3: bit 6 follows the EAR output (bit 4 of the last #FE write)
TEST_F(TapeIdleEar_Test, FerrantiUlaFollowsEarOutput)
{
    for (const char* model : { "48K", "128k", "PLUS2" })
    {
        SCOPED_TRACE(model);
        PortDecoder* ports = Create(model);
        ASSERT_NE(ports, nullptr);

        ports->DecodePortOut(0x00FE, 0x07, 0x8000);
        EXPECT_EQ(ports->DecodePortIn(kPortFE, 0x8000) & kEar, 0);
        ports->DecodePortOut(0x00FE, 0x17, 0x8000);
        EXPECT_EQ(ports->DecodePortIn(kPortFE, 0x8000) & kEar, kEar);

        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

// Other boards read LOW with no tape image, whatever the EAR output
TEST_F(TapeIdleEar_Test, NoTapeImageReadsLow)
{
    for (const char* model : { "PENTAGON", "PLUS3" })
    {
        SCOPED_TRACE(model);
        PortDecoder* ports = Create(model);
        ASSERT_NE(ports, nullptr);

        ports->DecodePortOut(0x00FE, 0x17, 0x8000);
        EXPECT_EQ(ports->DecodePortIn(kPortFE, 0x8000) & kEar, 0);

        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

// The Scorpion's input is pulled up: the ProfROM monitor reads bit 6 = 0 as "no signal" (error #61)
TEST_F(TapeIdleEar_Test, ScorpionReadsHigh)
{
    for (const char* model : { "SCORPION", "PROFSCORP" })
    {
        SCOPED_TRACE(model);
        PortDecoder* ports = Create(model);
        ASSERT_NE(ports, nullptr);

        ports->DecodePortOut(0x00FE, 0x07, 0x8000);
        EXPECT_EQ(ports->DecodePortIn(kPortFE, 0x8000) & kEar, kEar);

        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}
