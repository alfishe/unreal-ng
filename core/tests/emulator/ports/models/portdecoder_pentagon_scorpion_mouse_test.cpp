#include "stdafx.h"
#include "gtest/gtest.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/models/portdecoder_pentagon1024.h"
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

/// region <Kempston joystick on the Pentagon family (tdd-kempston-joystick.md section 3)>

class PortDecoder_PentagonJoystick_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext();
        _mouse = new Mouse(_context);
        _context->pMouse = _mouse;
        _joystick = new Joystick(_context);
        _joystick->SetPresent(true);
        _context->pJoystick = _joystick;
    }

    void TearDown() override
    {
        _context->pJoystick = nullptr;
        delete _joystick;
        delete _mouse;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    Mouse* _mouse = nullptr;
    Joystick* _joystick = nullptr;
};

/// JOY-P1: outside TR-DOS the exact low byte #1F reads the stick; idle is 0x00; the mouse is not disturbed
TEST_F(PortDecoder_PentagonJoystick_Test, Pentagon128_ReadsTheStickOutsideTrDos)
{
    PortDecoder_Pentagon128 decoder(_context);
    EXPECT_TRUE(decoder.HasKempstonJoystick());
    _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);

    EXPECT_EQ(decoder.DecodePortIn(0x001F, 0x0000), 0x00);
    _joystick->SetState(Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(decoder.DecodePortIn(0x001F, 0x0000), 0x18);
    EXPECT_TRUE(decoder.WasLastPortDecoded());
    EXPECT_EQ(decoder.DecodePortIn(0xFB1F, 0x0000), 0x18) << "the full low byte is the joystick whatever A15..A8 say";

    EXPECT_EQ(decoder.DecodePortIn(0xFBDF, 0x0000), 31) << "the mouse addresses are unchanged";
    EXPECT_NE(decoder.DecodePortIn(0x003F, 0x0000), 0x18) << "#3F is not the joystick";
}

/// JOY-P2: with TR-DOS in, #1F is the Beta Disk status again
TEST_F(PortDecoder_PentagonJoystick_Test, Pentagon128_TrDosHandsTheAddressToTheFdc)
{
    PortDecoder_Pentagon128 decoder(_context);
    _joystick->SetState(0x1F);
    _context->emulatorState.flags |= CF_TRDOS | CF_DOSPORTS;
    EXPECT_NE(decoder.DecodePortIn(0x001F, 0x0000), 0x1F);
    _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    EXPECT_EQ(decoder.DecodePortIn(0x001F, 0x0000), 0x1F);
}

/// JOY-P3: not fitted (or no device): nothing drives the bus, the read stays undecoded as before
TEST_F(PortDecoder_PentagonJoystick_Test, Pentagon128_NotFittedLeavesThePortUndecoded)
{
    PortDecoder_Pentagon128 decoder(_context);
    _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    _joystick->SetState(0x1F);
    _joystick->SetPresent(false);
    decoder.DecodePortIn(0x001F, 0x0000);
    EXPECT_FALSE(decoder.WasLastPortDecoded());

    _joystick->SetPresent(true);
    _context->pJoystick = nullptr;
    decoder.DecodePortIn(0x001F, 0x0000);
    EXPECT_FALSE(decoder.WasLastPortDecoded());
    _context->pJoystick = _joystick;
}

/// JOY-P4: Pentagon 1024 (and 512) inherit the same arm
TEST_F(PortDecoder_PentagonJoystick_Test, Pentagon1024_ReadsTheStick)
{
    PortDecoder_Pentagon1024 decoder(_context);
    _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    _joystick->SetState(Joystick::kRight);
    EXPECT_EQ(decoder.DecodePortIn(0x001F, 0x0000), 0x01);
}

/// endregion </Kempston joystick on the Pentagon family>
