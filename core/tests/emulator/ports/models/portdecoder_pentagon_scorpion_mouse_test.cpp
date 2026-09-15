#include "stdafx.h"
#include "gtest/gtest.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

class PortDecoder_PentagonScorpionMouse_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext();
        _mouse = new Mouse(_context);
        _context->pMouse = _mouse;
    }

    void TearDown() override
    {
        delete _mouse;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    Mouse* _mouse = nullptr;
};

TEST_F(PortDecoder_PentagonScorpionMouse_Test, Pentagon128_StandardDecodeAndMirrors)
{
    PortDecoder_Pentagon128 decoder(_context);

    // Initial mouse state: buttons 0xFF (no wheel fitted), X 31, Y 85
    EXPECT_EQ(decoder.DecodePortIn(0xFADF, 0x0000), 0xFF); // Buttons (#FADF)
    EXPECT_EQ(decoder.DecodePortIn(0x7ADF, 0x0000), 0xFF); // Buttons mirror (#7ADF, A15=0)
    EXPECT_EQ(decoder.DecodePortIn(0xFEDF, 0x0000), 0xFF); // Buttons alias (#FEDF, A10=1)

    EXPECT_EQ(decoder.DecodePortIn(0xFBDF, 0x0000), 31);   // X axis (#FBDF)
    EXPECT_EQ(decoder.DecodePortIn(0x7BDF, 0x0000), 31);   // X axis mirror (#7BDF, A15=0)

    EXPECT_EQ(decoder.DecodePortIn(0xFFDF, 0x0000), 85);   // Y axis (#FFDF)
    EXPECT_EQ(decoder.DecodePortIn(0x7FDF, 0x0000), 85);   // Y axis mirror (#7FDF, A15=0)
}

TEST_F(PortDecoder_PentagonScorpionMouse_Test, Pentagon128_TRDOSGating)
{
    PortDecoder_Pentagon128 decoder(_context);

    // Without CF_DOSPORTS, mouse reads answer
    EXPECT_EQ(decoder.DecodePortIn(0xFBDF, 0x0000), 31);

    // Raise CF_DOSPORTS -> mouse is gated (off bus)
    _context->emulatorState.flags |= CF_DOSPORTS;
    EXPECT_NE(decoder.DecodePortIn(0xFBDF, 0x0000), 31);
}

TEST_F(PortDecoder_PentagonScorpionMouse_Test, Scorpion256_StandardDecodeAndMirrors)
{
    PortDecoder_Scorpion256 decoder(_context);

    EXPECT_EQ(decoder.DecodePortIn(0xFADF, 0x0000), 0xFF); // Buttons
    EXPECT_EQ(decoder.DecodePortIn(0xFBDF, 0x0000), 31);   // X axis
    EXPECT_EQ(decoder.DecodePortIn(0xFFDF, 0x0000), 85);   // Y axis
}

TEST_F(PortDecoder_PentagonScorpionMouse_Test, Scorpion256_JoystickNarrowedTo1F)
{
    PortDecoder_Scorpion256 decoder(_context);

    // Joystick answers port #FF1F specifically
    EXPECT_TRUE(decoder.IsPort_KempstonJoystick(0xFF1F));

    // Mouse port #FFDF is NOT claimed as joystick
    EXPECT_FALSE(decoder.IsPort_KempstonJoystick(0xFFDF));

    // Mouse port #FFDF is claimed as mouse
    uint8_t reg = 0;
    EXPECT_TRUE(decoder.IsPort_KempstonMouse(0xFFDF, reg));
    EXPECT_EQ(reg, 2); // Y axis register
}
