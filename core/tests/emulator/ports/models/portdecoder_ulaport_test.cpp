// Port #FE on the stock Spectrums (48K, +2A, +3): the ULA port must reach the border, the pFE latch
// and the beeper, the same as on every other model.

#include <gtest/gtest.h>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/soundmanager.h"

namespace
{
class PortDecoderUlaPort_Test : public ::testing::TestWithParam<const char*>
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ulaport", model, LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
};
}  // namespace

/// An OUT to #FE sets pFE and the border attribute
TEST_P(PortDecoderUlaPort_Test, OutToFeLatchesTheBorderAndPFE)
{
    Create(GetParam());
    ASSERT_FALSE(HasFatalFailure());

    _context->pPortDecoder->DecodePortOut(0x00FE, 0x15, 0x8000);

    EXPECT_EQ(_context->emulatorState.pFE, 0x15);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x05);
}

/// The EAR bit toggled by an OUT to #FE produces sound in the beeper (the 48K beeper was mute)
TEST_P(PortDecoderUlaPort_Test, OutToFeReachesTheBeeper)
{
    Create(GetParam());
    ASSERT_FALSE(HasFatalFailure());

    Beeper& beeper = _context->pSoundManager->getBeeper();
    beeper.handleFrameStart();
    _context->pPortDecoder->DecodePortOut(0x00FE, 0x00, 0x8000);
    _context->pPortDecoder->DecodePortOut(0x00FE, 0x10, 0x8000);
    beeper.handleFrameEnd(_context->config.frame);

    EXPECT_TRUE(beeper.hadSoundLastFrame());
}

INSTANTIATE_TEST_SUITE_P(StockSpectrums, PortDecoderUlaPort_Test, ::testing::Values("48K", "PLUS2A", "PLUS3"));
