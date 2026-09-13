#include "stdafx.h"
#include "gtest/gtest.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"

class Mouse_Test : public ::testing::Test
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

TEST_F(Mouse_Test, InitialResetCoordinates)
{
    // Hardware reference specifies reset coords X=31, Y=85 to avoid equal-axis presence heuristics
    EXPECT_EQ(_mouse->GetX(), 31);
    EXPECT_EQ(_mouse->GetY(), 85);
    EXPECT_EQ(_mouse->GetButtons(), 0xFF);
    EXPECT_EQ(_mouse->GetWheel(), 0x00);
    EXPECT_TRUE(_mouse->IsPresent());
}

TEST_F(Mouse_Test, ReadRegisterSemantics)
{
    // Register 0, no wheel fitted (default): D7-D3 read 1 as part of the #FF base
    EXPECT_FALSE(_mouse->IsWheelEnabled());
    EXPECT_EQ(_mouse->ReadRegister(0), 0xFF);

    // Register 1: X axis
    EXPECT_EQ(_mouse->ReadRegister(1), 31);

    // Register 2: Y axis
    EXPECT_EQ(_mouse->ReadRegister(2), 85);
}

TEST_F(Mouse_Test, AxisWrapping)
{
    _mouse->Move(230, 200); // 31 + 230 = 261 -> 5; 85 + 200 = 285 -> 29 (uint8 wrapping)
    EXPECT_EQ(_mouse->GetX(), 5);
    EXPECT_EQ(_mouse->ReadRegister(1), 5);
    EXPECT_EQ(_mouse->GetY(), 29);
    EXPECT_EQ(_mouse->ReadRegister(2), 29);

    _mouse->Move(-10, -90); // 5 - 10 = 251; 29 - 90 = 195 (uint8 wrapping)
    EXPECT_EQ(_mouse->GetX(), 251);
    EXPECT_EQ(_mouse->ReadRegister(1), 251);
    EXPECT_EQ(_mouse->GetY(), 195);
    EXPECT_EQ(_mouse->ReadRegister(2), 195);
}

TEST_F(Mouse_Test, ActiveLowButtonMasks)
{
    // D0=Left, D1=Right, D2=Middle (active low)
    _mouse->SetButtons(0xFE); // Left pressed (D0=0)
    EXPECT_EQ(_mouse->ReadRegister(0), 0xFE); // no wheel: D7-D3 = 1, buttons = 0x06

    _mouse->SetButtons(0xF8); // All 3 pressed
    EXPECT_EQ(_mouse->ReadRegister(0), 0xF8);
}

TEST_F(Mouse_Test, WheelNibbleAssembly)
{
    // Wheel-equipped interface: {wheel[3:0], 1'b1, buttons[2:0]}
    _mouse->SetWheelEnabled(true);
    _mouse->SetWheel(3);
    // wheel=3 (0x30), bit3=1, buttons=0x07 -> 0x3F
    EXPECT_EQ(_mouse->ReadRegister(0), 0x3F);

    _mouse->SetWheel(15); // 3 + 15 = 18 -> 2 (4-bit wrapping)
    EXPECT_EQ(_mouse->GetWheel(), 2);
    EXPECT_EQ(_mouse->ReadRegister(0), 0x2F);
}

/// Without a wheel the counter still accumulates but is not visible on the bus;
/// the upper bits must keep reading 1 (software detection, e.g. ProfROM #08FB)
TEST_F(Mouse_Test, WheelHiddenWhenNotFitted)
{
    _mouse->SetWheel(5);
    EXPECT_EQ(_mouse->ReadRegister(0), 0xFF);
    EXPECT_EQ(_mouse->ReadRegister(0) & 0x38, 0x38);

    _mouse->SetWheelEnabled(true);
    EXPECT_EQ(_mouse->ReadRegister(0), 0x5F);

    _mouse->Reset();
    EXPECT_TRUE(_mouse->IsWheelEnabled()) << "wheel fitting survives reset";
}

TEST_F(Mouse_Test, AbsenceReturnsFF)
{
    _mouse->SetPresent(false);
    EXPECT_FALSE(_mouse->IsPresent());
    EXPECT_EQ(_mouse->ReadRegister(0), 0xFF);
    EXPECT_EQ(_mouse->ReadRegister(1), 0xFF);
    EXPECT_EQ(_mouse->ReadRegister(2), 0xFF);
}

TEST_F(Mouse_Test, MessageCenterEvents)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Post movement
    mc.Post(MC_MOUSE_MOVE, MouseEvent::Move(10, 20));
    EXPECT_TRUE(TestWait::For([this] { return _mouse->GetX() == 41 && _mouse->GetY() == 105; }));
    EXPECT_EQ(_mouse->GetX(), 41);
    EXPECT_EQ(_mouse->GetY(), 105);

    // Post button state
    mc.Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xFD)); // Right pressed
    EXPECT_TRUE(TestWait::For([this] { return _mouse->GetButtons() == 0xFD; }));
    EXPECT_EQ(_mouse->GetButtons(), 0xFD);

    // Post wheel delta
    mc.Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(1));
    EXPECT_TRUE(TestWait::For([this] { return _mouse->GetWheel() == 1; }));
    EXPECT_EQ(_mouse->GetWheel(), 1);
}

