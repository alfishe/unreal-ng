// The Next's extended keys and the MD pads' extra buttons (NR #B0, #B1, #B2; zxnext.vhd 6155-6167; membrane.vhd o_extended_keys; keymaps.vhd).
// The host's physical keys arrive the way a front end sends them: PcKeyEvent posted to the MessageCenter (PCKEY_PRESSED / PCKEY_RELEASED).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/z80n/nextextendedinput.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

namespace
{
constexpr uint8_t kRegExtended0 = 0xB0, kRegExtended1 = 0xB1, kRegPads = 0xB2;
constexpr auto kDeliveryTimeout = std::chrono::milliseconds(2000);

// NR #B0 bits 7..0: ; " , . UP DOWN LEFT RIGHT   NR #B1 bits 7..0: DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
constexpr uint8_t kB0Semicolon = 0x80, kB0Quote = 0x40, kB0Comma = 0x20, kB0Period = 0x10, kB0Up = 0x08, kB0Down = 0x04, kB0Left = 0x02, kB0Right = 0x01;
constexpr uint8_t kB1Delete = 0x80, kB1Edit = 0x40, kB1Break = 0x20, kB1Inv = 0x10, kB1True = 0x08, kB1Graph = 0x04, kB1CapsLock = 0x02, kB1Extend = 0x01;
// NR #B2: the right pad X Z Y MODE in bits 7..4, the left pad's in bits 3..0
constexpr unsigned kLeftShift = 0, kRightShift = 4;
}  // namespace

class NextExtendedInput_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The physical key, as the front end posts it, for this machine only
    void Post(PcKey key, bool pressed)
    {
        MessageCenter& center = MessageCenter::DefaultMessageCenter();
        const KeyEventEnum type = pressed ? KEY_PRESSED : KEY_RELEASED;
        center.Post(pressed ? MC_PCKEY_PRESSED : MC_PCKEY_RELEASED, new PcKeyEvent(static_cast<uint8_t>(key), type, _emulator->GetUUID()));
    }
    /// Wait until the register reads `value` (the message center delivers on its own thread)
    bool WaitFor(uint8_t reg, uint8_t value)
    {
        return TestWait::For([&] { return _ports->Board().Read(reg) == value; }, kDeliveryTimeout);
    }
};

TEST_F(NextExtendedInput_Test, TheHostsPhysicalKeysAppearInNrB0AndNrB1AsTheKeymapPlacesThem)
{
    EXPECT_EQ(_ports->Board().Read(kRegExtended0), 0);
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), 0);

    // keymaps.vhd (U.S. PS/2): the arrows, ; ' , . as themselves
    for (PcKey key : {PcKey::Up, PcKey::Semicolon, PcKey::Comma})
        Post(key, true);
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Up | kB0Semicolon | kB0Comma));
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), 0) << "B1 holds other keys";
    for (PcKey key : {PcKey::Down, PcKey::Left, PcKey::Right, PcKey::Quote, PcKey::Period})
        Post(key, true);
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Up | kB0Down | kB0Left | kB0Right | kB0Semicolon | kB0Quote | kB0Comma | kB0Period));

    // Left Alt = EXTEND, Right Alt = GRAPH, Caps Lock, Backspace = DELETE, Esc = BREAK, ` = EDIT, Tab = TRUE VIDEO, \ = INV VIDEO
    for (PcKey key : {PcKey::LeftAlt, PcKey::RightAlt, PcKey::CapsLock, PcKey::Backspace, PcKey::Escape, PcKey::Backquote, PcKey::Tab, PcKey::Backslash})
        Post(key, true);
    ASSERT_TRUE(WaitFor(kRegExtended1, kB1Extend | kB1Graph | kB1CapsLock | kB1Delete | kB1Break | kB1Edit | kB1True | kB1Inv));

    // The program reads them as any NextREG: select on #243B, read #253B
    constexpr uint16_t kPortSelect = PortDecoder_Next::kPortRegSelect, kPortData = PortDecoder_Next::kPortRegData;
    _ports->DecodePortOut(kPortSelect, kRegExtended0, 0);
    EXPECT_EQ(_ports->DecodePortIn(kPortData, 0), kB0Up | kB0Down | kB0Left | kB0Right | kB0Semicolon | kB0Quote | kB0Comma | kB0Period);

    // Released keys leave the registers
    for (PcKey key : {PcKey::Up, PcKey::LeftAlt})
        Post(key, false);
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Down | kB0Left | kB0Right | kB0Semicolon | kB0Quote | kB0Comma | kB0Period));
    ASSERT_TRUE(WaitFor(kRegExtended1, kB1Graph | kB1CapsLock | kB1Delete | kB1Break | kB1Edit | kB1True | kB1Inv));

    // A key the table does not know changes nothing; the registers are read only
    Post(PcKey::Function5, true);
    _ports->Board().WriteNextReg(kRegExtended0, 0xFF);
    EXPECT_EQ(_ports->Board().Read(kRegExtended0), kB0Down | kB0Left | kB0Right | kB0Semicolon | kB0Quote | kB0Comma | kB0Period);
}

TEST_F(NextExtendedInput_Test, ThePadsModeXYAndZAreInNrB2PerSideAndTheHostPadIsTheLeftOne)
{
    constexpr uint8_t kMode = NextExtendedInput::kPadMode, kY = NextExtendedInput::kPadY, kZ = NextExtendedInput::kPadZ, kX = NextExtendedInput::kPadX;
    EXPECT_EQ(_ports->Board().Read(kRegPads), 0);

    // the host's pad: keypad 3 / 9 / 1 / 7 by default, on the left connector
    Post(PcKey::Keypad3, true);
    ASSERT_TRUE(WaitFor(kRegPads, kMode << kLeftShift));
    Post(PcKey::Keypad7, true);
    ASSERT_TRUE(WaitFor(kRegPads, (kMode | kX) << kLeftShift));
    Post(PcKey::Keypad9, true);
    Post(PcKey::Keypad1, true);
    ASSERT_TRUE(WaitFor(kRegPads, (kMode | kX | kY | kZ) << kLeftShift));
    Post(PcKey::Keypad7, false);
    ASSERT_TRUE(WaitFor(kRegPads, (kMode | kY | kZ) << kLeftShift));

    // the right connector has no host input: a tool sets it, and it lands in bits 7:4
    _ports->ExtendedInput().SetPad(false, kX | kMode);
    EXPECT_EQ(_ports->Board().Read(kRegPads), ((kX | kMode) << kRightShift) | ((kMode | kY | kZ) << kLeftShift));

    // all keys released at once
    _ports->ExtendedInput().ReleaseAllPcKeys();
    EXPECT_EQ(_ports->Board().Read(kRegPads), (kX | kMode) << kRightShift) << "the right pad is not a host key";
}

TEST_F(NextExtendedInput_Test, TheStateTravelsInTheTtdImageAndTheReportNamesTheKeys)
{
    Post(PcKey::Left, true);
    Post(PcKey::Keypad9, true);
    // Both keys before the image is taken: the message center delivers them one by one on its own thread, and under
    // load the second (the pad's Y) could still be on its way when only the first was waited for
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Left));
    ASSERT_TRUE(WaitFor(kRegPads, NextExtendedInput::kPadY));
    const std::vector<uint8_t> held = NextStateCodec::Save(*_ports);

    const StateNode report = DeviceState::NextInputReport(_context);
    const StateNode* keys = report.find("extended_keys");
    ASSERT_NE(keys, nullptr);
    ASSERT_EQ(keys->find("pressed")->items.size(), 1u);
    EXPECT_EQ(keys->find("pressed")->items[0].s, "left");
    EXPECT_EQ(keys->find("nr_b0")->s, "0x02");
    EXPECT_TRUE(report.find("md_pad_extra")->find("left")->find("y")->b);
    EXPECT_FALSE(report.find("md_pad_extra")->find("left")->find("x")->b);

    _ports->ExtendedInput().ReleaseAllPcKeys();
    EXPECT_EQ(_ports->Board().Read(kRegExtended0), 0);
    ASSERT_TRUE(NextStateCodec::Load(*_ports, held.data(), held.size()));
    EXPECT_EQ(_ports->Board().Read(kRegExtended0), kB0Left) << "a seek puts the held keys back";
    EXPECT_EQ(_ports->Board().Read(kRegPads), NextExtendedInput::kPadY);
}

// The joystick automation (DebugJoystickManager, every plane's joystick verbs) reaches the pads: the right connector, a, c, start, x, y, z, mode.
// Every change is a journaled input event (TTDInputKind::NextPad), applied by the machine
#include "debugger/joystick/debugjoystickmanager.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/io/joystick/joystick.h"

namespace
{
constexpr unsigned kLeftConnector = 0, kRightConnector = 1;
constexpr uint16_t kPadRight = 1u << 0, kPadUp = 1u << 3, kPadFire = 1u << 4, kPadC = 1u << 5, kPadA = 1u << 6, kPadStart = 1u << 7;
constexpr uint16_t kPadY = 1u << 8, kPadMode = 1u << 11;
constexpr unsigned kRegJoystickModes = 0x05;
constexpr uint8_t RightMode(unsigned code) { return static_cast<uint8_t>(((code & 4) >> 1) | ((code & 3) << 4)); }
constexpr uint8_t LeftMode(unsigned code) { return static_cast<uint8_t>(((code & 4) << 1) | ((code & 3) << 6)); }
constexpr unsigned kKempston1 = 1, kMd1 = 5, kSinclair2 = 0;
constexpr uint16_t kPortKempston1 = 0x001F;
}  // namespace

TEST_F(NextExtendedInput_Test, TheJoystickVerbsPressTheRightConnectorAndThePadButtons)
{
    DebugJoystickManager manager(_context);
    ASSERT_EQ(manager.GetState().hasPads, true);

    // The left pad: a / c / start are b6 / b5 / b7 of the Kempston byte, x y z mode the extras
    ASSERT_TRUE(manager.Press("a+start+y").ok());
    EXPECT_EQ(_ports->PadState(kLeftConnector), kPadA | kPadStart | kPadY);
    EXPECT_EQ(_context->pJoystick->State(), kPadA | kPadStart) << "the Kempston byte holds A and START";
    ASSERT_TRUE(manager.Release("a").ok());
    EXPECT_EQ(_ports->PadState(kLeftConnector), kPadStart | kPadY);

    // The right connector: every button, set by a tool; MODE X Y Z show in NR #B2's high nibble
    ASSERT_TRUE(manager.Press("right.up+right.fire+right.mode").ok());
    EXPECT_EQ(_ports->PadState(kRightConnector), kPadUp | kPadFire | kPadMode);
    EXPECT_EQ(_ports->Board().Read(kRegPads) >> kRightShift, NextExtendedInput::kPadMode);
    EXPECT_EQ(_ports->PadState(kLeftConnector), kPadStart | kPadY) << "the left pad is untouched";

    // Port #1F: the right side is Kempston 1 (and the left is not): the right pad's stick answers, bits 7:6 only for an MD pad
    _ports->Board().WriteNextReg(kRegJoystickModes, LeftMode(kSinclair2) | RightMode(kKempston1));
    EXPECT_EQ(_ports->DecodePortIn(kPortKempston1, 0), kPadUp | kPadFire);
    ASSERT_TRUE(manager.Press("right.start").ok());
    EXPECT_EQ(_ports->DecodePortIn(kPortKempston1, 0), kPadUp | kPadFire) << "a Kempston stick has no START";
    _ports->Board().WriteNextReg(kRegJoystickModes, LeftMode(kSinclair2) | RightMode(kMd1));
    EXPECT_EQ(_ports->DecodePortIn(kPortKempston1, 0), kPadUp | kPadFire | kPadStart);

    // set = exactly these, both connectors; release_all clears both
    ASSERT_TRUE(manager.SetNames("right.right+x").ok());
    EXPECT_EQ(_ports->PadState(kRightConnector), kPadRight);
    EXPECT_EQ(_ports->PadState(kLeftConnector), 1u << 10);
    ASSERT_TRUE(manager.ReleaseAll().ok());
    EXPECT_EQ(_ports->PadState(kLeftConnector), 0);
    EXPECT_EQ(_ports->PadState(kRightConnector), 0);

    // a timed tap on the right connector releases it again
    ASSERT_TRUE(manager.Tap("right.c", 1).ok());
    EXPECT_EQ(_ports->PadState(kRightConnector), kPadC);
    manager.OnFrame();
    EXPECT_EQ(_ports->PadState(kRightConnector), 0);

    // the report and the state snapshot name them
    ASSERT_TRUE(manager.Press("right.down").ok());
    EXPECT_EQ(manager.GetState().rightButtons, (std::vector<std::string>{"down"}));
    const StateNode report = DeviceState::NextInputReport(_context);
    ASSERT_EQ(report.find("right_pad")->find("buttons")->items.size(), 1u);
    EXPECT_EQ(report.find("right_pad")->find("buttons")->items[0].s, "down");

    // an unknown name is refused with the list
    const JoystickInjectResult bad = manager.Press("right.turbo");
    EXPECT_FALSE(bad.ok());
    EXPECT_NE(bad.message.find("right.<button>"), std::string::npos) << bad.message;
}

TEST_F(NextExtendedInput_Test, ThePadEventIsAJournaledInputAndAppliesByItself)
{
    // The event the automation submits (TTDInputKind::NextPad): applying it sets the connector, as a replay does
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::NextPad;
    ev.key = kRightConnector;
    ev.dx = static_cast<int16_t>(kPadUp | kPadMode);
    ttd::TTDInputDevices devices = ttd::InputDevicesOf(_context);
    ASSERT_TRUE(ttd::ApplyInputEvent(ev, devices));
    EXPECT_EQ(_ports->PadState(kRightConnector), kPadUp | kPadMode);
    ev.key = 7;  // no such connector
    EXPECT_FALSE(ttd::ApplyInputEvent(ev, devices));

    // the image carries the right pad
    const std::vector<uint8_t> held = NextStateCodec::Save(*_ports);
    _ports->ExtendedInput().SetRight(0);
    ASSERT_TRUE(NextStateCodec::Load(*_ports, held.data(), held.size()));
    EXPECT_EQ(_ports->PadState(kRightConnector), kPadUp | kPadMode);
}

// The membrane's extended keys as ZX keys (the host keyboard's matrix side, the on-screen keyboard, automation): one key each, held by the
// machine, which presses its two matrix keys (membrane.vhd 157-238) unless NR #68 bit 4 cancels them, and shows it in NR #B0 / #B1 either way
namespace
{
constexpr uint16_t kRowCapsShift = 0xFEFE, kRow12345 = 0xF7FE, kRowSpaceSymbol = 0x7FFE;
constexpr uint8_t kRegUlaControl = 0x68, kCancelExtendedKeys = 0x10;
}  // namespace

TEST_F(NextExtendedInput_Test, AMembraneKeyPressesItsMatrixKeysAndShowsInNrB0)
{
    Keyboard* keyboard = _context->pKeyboard;
    auto row = [&](uint16_t port) { return static_cast<uint8_t>(keyboard->HandlePortIn(port) & 0x1F); };
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    auto post = [&](ZXKeysEnum key, bool pressed) {
        center.Post(pressed ? MC_KEY_PRESSED : MC_KEY_RELEASED,
                    new KeyboardEvent(static_cast<uint8_t>(key), pressed ? KEY_PRESSED : KEY_RELEASED, _emulator->GetUUID()));
    };

    post(ZXKEY_EXT_LEFT, true);
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Left));
    EXPECT_EQ(row(kRowCapsShift), 0x1E) << "CAPS SHIFT";
    EXPECT_EQ(row(kRow12345), 0x0F) << "5";
    EXPECT_EQ(_ports->ExtendedInput().PcKeys(), 0) << "a ZX key, not a PC key";

    // NR #68 bit 4: the matrix entries go, the register keeps the key
    _ports->Board().Write(kRegUlaControl, kCancelExtendedKeys);
    EXPECT_EQ(row(kRowCapsShift), 0x1F);
    EXPECT_EQ(row(kRow12345), 0x1F);
    EXPECT_EQ(_ports->Board().Read(kRegExtended0), kB0Left);
    _ports->Board().Write(kRegUlaControl, 0);

    post(ZXKEY_EXT_LEFT, false);
    ASSERT_TRUE(WaitFor(kRegExtended0, 0));
    EXPECT_EQ(row(kRowCapsShift), 0x1F);
    EXPECT_EQ(row(kRow12345), 0x1F);

    // EXTEND MODE is CAPS SHIFT + SYMBOL SHIFT, GRAPH CAPS SHIFT + 9: the keys the 48K's matrix map has no code for
    keyboard->PressKey(ZXKEY_EXT_EXTEND);
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), kB1Extend);
    EXPECT_EQ(row(kRowCapsShift), 0x1E);
    EXPECT_EQ(row(kRowSpaceSymbol), 0x1D) << "SYMBOL SHIFT";
    const std::vector<uint8_t> held = NextStateCodec::Save(*_ports);
    keyboard->ReleaseKey(ZXKEY_EXT_EXTEND);
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), 0);
    EXPECT_EQ(row(kRowSpaceSymbol), 0x1F);
    ASSERT_TRUE(NextStateCodec::Load(*_ports, held.data(), held.size()));
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), kB1Extend) << "a seek puts the membrane's keys back";
    EXPECT_EQ(row(kRowSpaceSymbol), 0x1D);

    keyboard->Reset();
    EXPECT_EQ(_ports->Board().Read(kRegExtended1), 0) << "a keyboard reset lets them go";
}

TEST_F(NextExtendedInput_Test, TwoHostPressesOfAMembraneKeyHoldItUntilTheLastRelease)
{
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    auto post = [&](ZXKeysEnum key, bool pressed) {
        center.Post(pressed ? MC_KEY_PRESSED : MC_KEY_RELEASED,
                    new KeyboardEvent(static_cast<uint8_t>(key), pressed ? KEY_PRESSED : KEY_RELEASED, _emulator->GetUUID()));
    };
    // The host keyboard and the on-screen keyboard both hold "."; then "," marks the end of the stream
    post(ZXKEY_EXT_DOT, true);
    post(ZXKEY_EXT_DOT, true);
    post(ZXKEY_EXT_DOT, false);
    post(ZXKEY_EXT_COMMA, true);
    ASSERT_TRUE(WaitFor(kRegExtended0, kB0Period | kB0Comma));
    post(ZXKEY_EXT_DOT, false);
    post(ZXKEY_EXT_COMMA, false);
    ASSERT_TRUE(WaitFor(kRegExtended0, 0));
}
