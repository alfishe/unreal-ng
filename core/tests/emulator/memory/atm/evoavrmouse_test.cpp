#include "stdafx.h"

#include <gtest/gtest.h>

#include "debugger/ttd/atm/ttdevomouse.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/memory/atm/evoavrmouse.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

/// @brief The ZX-Evo AVR's PS/2 mouse (evoavrmouse.h), following pentevo
/// avr/current/ps2.c and zx.c.

class EvoAvrMouse_Test : public ::testing::Test
{
protected:
    EvoAvr _avr;
    EvoAvrMouse& Mouse() { return _avr.Ps2Mouse(); }
};

/// zx_mouse_reset: a found mouse reads X = 0, Y = 1; none reads #FF everywhere
TEST_F(EvoAvrMouse_Test, ResetValuesTellDetectionWhetherAMouseIsThere)
{
    EXPECT_FALSE(Mouse().IsConnected());
    EXPECT_EQ(Mouse().ReadRegister(1), 0xFF);
    EXPECT_EQ(Mouse().ReadRegister(2), 0xFF);
    EXPECT_EQ(Mouse().ReadRegister(0), 0xFF);

    Mouse().SetConnected(true);
    EXPECT_EQ(Mouse().ReadRegister(1), 0x00);
    EXPECT_EQ(Mouse().ReadRegister(2), 0x01);
    EXPECT_EQ(Mouse().ReadRegister(0), 0xFF);

    Mouse().OnMouseMotion(5, 5);
    Mouse().SetConnected(true);  // no change: the registers stay
    EXPECT_EQ(Mouse().ReadRegister(1), 0x05);

    Mouse().SetConnected(false);
    EXPECT_EQ(Mouse().ReadRegister(1), 0xFF);
    Mouse().OnMouseMotion(5, 5);
    EXPECT_EQ(Mouse().ReadRegister(1), 0xFF) << "nothing plugged in: no packets";
}

/// The header's worked example: resolution 1, 3 right, 1 down, left held
TEST_F(EvoAvrMouse_Test, PacketsLandInTheRegisters)
{
    Mouse().SetConnected(true);
    _avr.SetCell(EvoAvrMouse::kResolutionCell, 1);
    Mouse().OnMouseMotion(3, -1);
    Mouse().OnMouseButtons(0xFE);
    EXPECT_EQ(Mouse().ReadRegister(1), 0x06);
    EXPECT_EQ(Mouse().ReadRegister(2), 0xFF) << "1 - 2 wraps";
    EXPECT_EQ(Mouse().ReadRegister(0), 0xFE) << "wheel nibble F, bit 3, L pressed";

    Mouse().OnMouseButtons(0xF9);  // right + middle
    EXPECT_EQ(Mouse().ReadRegister(0) & 0x0F, 0x09);

    // Wheel: Z is negative away from the user, added to the high nibble
    Mouse().OnMouseWheel(1);  // away: F - 1
    EXPECT_EQ(Mouse().ReadRegister(0) >> 4, 0x0E);
    Mouse().OnMouseWheel(-3);  // toward: E + 3 = 1 (mod 16)
    EXPECT_EQ(Mouse().ReadRegister(0) >> 4, 0x01);
    EXPECT_EQ(Mouse().ReadRegister(0) & 0x0F, 0x09) << "the buttons stay";
}

/// ps2mouse_set_resolution through the keyboard parser: keypad + / - / * with both buttons held
TEST_F(EvoAvrMouse_Test, KeypadSetsTheResolutionWithBothButtonsHeld)
{
    Mouse().SetConnected(true);
    _avr.ReceivePs2Byte(0x79);  // '+' without the buttons: nothing
    _avr.ReceivePs2Byte(0xF0);
    _avr.ReceivePs2Byte(0x79);
    EXPECT_EQ(Mouse().Resolution(), 0);

    Mouse().OnMouseButtons(0xFC);  // left + right
    for (int i = 0; i < 5; i++)
    {
        _avr.ReceivePs2Byte(0x79);  // '+' make
        _avr.ReceivePs2Byte(0xF0);  // break
        _avr.ReceivePs2Byte(0x79);
    }
    EXPECT_EQ(Mouse().Resolution(), 3) << "stops at 8 counts/mm";
    _avr.ReceivePs2Byte(0x7B);  // '-'
    _avr.ReceivePs2Byte(0xF0);
    _avr.ReceivePs2Byte(0x7B);
    EXPECT_EQ(Mouse().Resolution(), 2);
    EXPECT_EQ(_avr.GetCell(EvoAvrMouse::kResolutionCell), 2) << "kept in the AVR's RTC cell #FD";

    _avr.ReceivePs2Byte(0xE0);  // E0 7C is Print Screen's tail, not keypad '*'
    _avr.ReceivePs2Byte(0x7C);
    EXPECT_EQ(Mouse().Resolution(), 2);
    _avr.ReceivePs2Byte(0x7C);  // keypad '*': the default
    EXPECT_EQ(Mouse().Resolution(), 0);

    Mouse().OnMouseMotion(1, 0);
    EXPECT_EQ(Mouse().ReadRegister(1), 0x01);
    _avr.SetCell(EvoAvrMouse::kResolutionCell, 3);
    Mouse().OnMouseMotion(1, 0);
    EXPECT_EQ(Mouse().ReadRegister(1), 0x09) << "8 counts per pixel at resolution 3";
}

/// TTD blob round trip
TEST_F(EvoAvrMouse_Test, TtdBlobRestoresTheRegisters)
{
    Mouse().SetConnected(true);
    Mouse().OnMouseMotion(10, 20);
    Mouse().OnMouseButtons(0xFD);
    ttd::TTDEvoMouse serializer(Mouse());
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    const uint64_t hash = serializer.TTDHashState();

    Mouse().OnMouseMotion(1, 1);
    Mouse().SetConnected(false);
    EXPECT_NE(serializer.TTDHashState(), hash);
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(serializer.TTDHashState(), hash);
    EXPECT_EQ(Mouse().ReadRegister(1), 10);
    EXPECT_EQ(Mouse().ReadRegister(2), 21);
    EXPECT_TRUE(Mouse().IsConnected());
}

/// TS-Conf: the mouse ports read the AVR's mouse, fed by the emulator's mouse manager
TEST(EvoAvrMouseMachine_Test, TsConfPortsReadTheAvrMouse)
{
    EmulatorManager* emulators = EmulatorManager::GetInstance();
    auto emulator = emulators->CreateEmulatorWithModelAndRAM("evo-mouse-ts", "TSL", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    ASSERT_TRUE(decoder->GetEvoAvr().Ps2Mouse().IsConnected());
    EXPECT_EQ(decoder->DecodePortIn(0xFBDF, 0), 0x00) << "found: X = 0";
    EXPECT_EQ(decoder->DecodePortIn(0xFFDF, 0), 0x01) << "found: Y = 1";

    context->pMouseManager->ApplyMotion(7, -2);
    context->pMouseManager->ApplyButtons(0xFD);
    EXPECT_EQ(decoder->DecodePortIn(0xFBDF, 0), 0x07);
    EXPECT_EQ(decoder->DecodePortIn(0xFFDF, 0), 0xFF);
    EXPECT_EQ(decoder->DecodePortIn(0xFADF, 0) & 0x0F, 0x0D);

    uint8_t value = 0;
    ASSERT_TRUE(decoder->PeekMouseRegister(1, value));
    EXPECT_EQ(value, 0x07) << "automation's status sees the same register";

    emulators->RemoveEmulator(emulator->GetId());
}
