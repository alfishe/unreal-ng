// The Next's audio and input reports and the board's small registers (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P5)
// Source: zxnext.vhd for NR #05 (nr_05_joy0 = bit 3 & bits 7:6, nr_05_joy1 = bit 1 & bits 5:4), the joystick enables (mdL_1f_en ... joyR_37_en),
// NR #0B (nr_0b_joy_iomode*), NR #86-#89 (the bus port enables); nextaudio.h for the chips' select / pan write (#FFFD bit 7 with bits 4:2 set)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../../automation/cli/src/commands/cli-next-format.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/nextaudio.h"
#include "emulator/state/devicestate.h"

namespace
{
// NextREG numbers and bits (the register map)
constexpr uint8_t kRegJoystickModes = 0x05;
constexpr uint8_t kRegPeripheral1Audio = 0x06;      // bits 1:0 the chip model: 00 YM, 01 AY
constexpr uint8_t kRegPeripheral2Audio = 0x08;      // bit 1 turbosound, bit 3 DAC, bit 5 ACB
constexpr uint8_t kRegMonoChips = 0x09;             // bits 7:5 mono per chip
constexpr uint8_t kRegJoystickIoMode = 0x0B;        // bit 7 enable, bits 5:4 mode, bit 0
constexpr uint8_t kRegBusEnable3 = 0x89;            // the last of the bus port enables; bit 7 chooses the reset that restores them
constexpr uint8_t kRegDebugLed = 0xFF;
constexpr uint8_t kTurboSound = 0x02, kDacOn = 0x08, kAcb = 0x20;
constexpr uint8_t kModelAy = 0x01;
constexpr uint8_t kMonoChip1 = 0x40;
// NR #05: the left joystick's mode is bit 3 (high) and bits 7:6 (low); the right's bit 1 and bits 5:4
constexpr uint8_t LeftMode(unsigned code) { return static_cast<uint8_t>(((code & 4) << 1) | ((code & 3) << 6)); }
constexpr uint8_t RightMode(unsigned code) { return static_cast<uint8_t>(((code & 4) >> 1) | ((code & 3) << 4)); }
constexpr unsigned kSinclair2 = 0, kKempston1 = 1, kKempston2 = 4, kMd1 = 5, kIoMode = 7;
// #FFFD: bit 7 and bits 4:2 set selects a chip; bits 1:0 the chip (3 = AY 0, 2 = AY 1, 1 = AY 2), bit 6 pan left, bit 5 pan right
constexpr uint8_t kSelectChipBits = 0x9C;
constexpr uint8_t kPanLeftBit = 0x40, kPanRightBit = 0x20;
constexpr uint8_t kChipAy1 = 2;
// AY registers: A's tone period is R0 (fine) and R1 (coarse, 4 bits)
constexpr uint8_t kRegToneFineA = 0, kRegToneCoarseA = 1;
constexpr uint8_t kDacSilence = 0x80;
}  // namespace

class NextAudioInput_Test : public ::testing::Test
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
    NextBoard& Board() { return _ports->Board(); }
};

TEST_F(NextAudioInput_Test, TheAudioReportShowsTheConfigurationTheChipsAndTheDac)
{
    Board().WriteNextReg(kRegPeripheral1Audio, kModelAy);
    Board().WriteNextReg(kRegPeripheral2Audio, kTurboSound | kDacOn | kAcb);
    Board().WriteNextReg(kRegMonoChips, kMonoChip1);
    NextAudio& audio = _ports->Audio();
    // chip 0 stereo (the default), tone A of 0x234; chip AY 1 (select value 2) panned left only
    audio.WriteSelect(kRegToneFineA);
    audio.WriteData(0x34);
    audio.WriteSelect(kRegToneCoarseA);
    audio.WriteData(0x02);
    audio.WriteSelect(kSelectChipBits | kPanLeftBit | kChipAy1);
    EXPECT_EQ(audio.SelectedChip(), 1u);
    audio.WriteDac(0, 0xC0);
    audio.WriteDac(1, 0x40);

    const StateNode report = DeviceState::NextAudioReport(_context);
    EXPECT_TRUE(report.find("available")->b);
    const StateNode* config = report.find("config");
    EXPECT_EQ(config->find("chip_model")->s, "ay8910");
    EXPECT_EQ(config->find("stereo_mode")->s, "acb");
    EXPECT_TRUE(config->find("turbosound")->b);
    EXPECT_TRUE(config->find("dac")->b);
    EXPECT_EQ(report.find("selected_chip")->i, 1);
    const StateNode* chips = report.find("chips");
    ASSERT_EQ(chips->items.size(), NextAudio::kChips);
    EXPECT_EQ(chips->items[0].find("tone_periods")->items[0].i, 0x234);
    EXPECT_EQ(chips->items[0].find("pan")->s, "stereo");
    EXPECT_FALSE(chips->items[0].find("mono")->b);
    EXPECT_EQ(chips->items[1].find("pan")->s, "left");
    EXPECT_TRUE(chips->items[1].find("mono")->b);
    EXPECT_TRUE(chips->items[1].find("selected")->b);
    EXPECT_EQ(chips->items[0].find("registers")->items.size(), 16u);
    const StateNode* dac = report.find("dac");
    EXPECT_EQ(dac->find("a")->s, "0xC0");
    EXPECT_EQ(dac->find("left_sum")->i, 0xC0 + 0x40);
    EXPECT_EQ(dac->find("right_sum")->i, 2 * kDacSilence) << "C and D hold the silence level";

    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); };
    const std::string text = run({"next", "audio"});
    EXPECT_NE(text.find("Sound: ay8910 acb, turbosound on"), std::string::npos) << text;
    EXPECT_NE(text.find("chip 1* pan left mono"), std::string::npos) << text;
    EXPECT_NE(text.find("tones 564"), std::string::npos) << text;
    EXPECT_NE(text.find("(left 256, right 256)"), std::string::npos) << text;
}

TEST_F(NextAudioInput_Test, TheInputReportDecodesTheJoystickModesAndTheKempstonPort)
{
    // power-on defaults (zxnext.vhd): the left joystick is Kempston 1, the right Sinclair 2
    StateNode report = DeviceState::NextInputReport(_context);
    EXPECT_EQ(report.find("joystick_modes")->find("left")->find("name")->s, "kempston1");
    EXPECT_EQ(report.find("joystick_modes")->find("left")->find("port")->s, "1F");
    EXPECT_EQ(report.find("joystick_modes")->find("right")->find("name")->s, "sinclair2");
    EXPECT_TRUE(report.find("kempston_1f")->find("selected_by_nr_05")->b);

    Board().WriteNextReg(kRegJoystickModes, LeftMode(kKempston2) | RightMode(kMd1));
    report = DeviceState::NextInputReport(_context);
    EXPECT_EQ(report.find("joystick_modes")->find("left")->find("name")->s, "kempston2");
    EXPECT_EQ(report.find("joystick_modes")->find("left")->find("port")->s, "37");
    EXPECT_EQ(report.find("joystick_modes")->find("right")->find("name")->s, "md1");
    EXPECT_EQ(report.find("joystick_modes")->find("right")->find("port")->s, "1F");
    EXPECT_TRUE(report.find("kempston_1f")->find("selected_by_nr_05")->b) << "the right side's MD pad answers on #1F";

    Board().WriteNextReg(kRegJoystickModes, LeftMode(kSinclair2) | RightMode(kSinclair2));
    report = DeviceState::NextInputReport(_context);
    EXPECT_FALSE(report.find("kempston_1f")->find("selected_by_nr_05")->b);
    EXPECT_FALSE(report.find("kempston_1f")->find("answers_here")->b) << "the board does not drive #1F with these modes";
    EXPECT_FALSE(report.find("kempston_1f")->find("note")->s.empty());

    Board().WriteNextReg(kRegJoystickModes, LeftMode(kIoMode));
    EXPECT_EQ(DeviceState::NextInputReport(_context).find("joystick_modes")->find("left")->find("name")->s, "io_mode");

    Board().WriteNextReg(kRegJoystickIoMode, 0x80 | 0x20);
    report = DeviceState::NextInputReport(_context);
    EXPECT_TRUE(report.find("io_mode")->find("enabled")->b);
    EXPECT_EQ(report.find("io_mode")->find("mode")->s, "pin7 = UART TX");
    EXPECT_TRUE(report.find("io_mode")->find("uart_on_joystick_port")->b);
    EXPECT_EQ(report.find("not_modelled"), nullptr) << "nothing of the input is left unmodelled";

    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); };
    const std::string text = run({"next", "input"});
    EXPECT_NE(text.find("left io_mode [user port]"), std::string::npos) << text;
    EXPECT_NE(text.find("I/O mode (NR #0B 0xA0): pin7 = UART TX"), std::string::npos) << text;
    EXPECT_NE(text.find("this emulator still answers"), std::string::npos) << text;
}

TEST_F(NextAudioInput_Test, TheBoardBlockShowsTheBusEnablesTheDebugLedAndTheUnmodelledIoTrap)
{
    Board().WriteNextReg(kRegBusEnable3, 0x80);
    Board().WriteNextReg(kRegDebugLed, 0x5A);
    const StateNode report = DeviceState::Next(_context);
    const StateNode* board = report.find("board");
    ASSERT_NE(board, nullptr);
    EXPECT_EQ(board->find("bus_port_enable")->find("nr_89")->s, "0x80");
    EXPECT_EQ(board->find("bus_port_enable")->find("restored_by")->s, "soft reset");
    EXPECT_NE(board->find("debug_led"), nullptr);
    EXPECT_TRUE(board->find("io_trap")->find("modelled")->b);
}

// zxnext.vhd 2396, 2616-2617, 3436-3470: the board drives #1F for Kempston 1 / an MD pad on #1F and #37 for Kempston 2 / an MD pad on #37,
// each with its bit of NR #82 (6 and 7); the host joystick is the left connector; bits 7:6 belong to an MD pad; a port nobody drives idles at #FF
namespace
{
constexpr uint16_t kPortKempston1 = 0x001F, kPortKempston2 = 0x0037;
constexpr uint8_t kStickRight = 0x01, kStickFire = 0x10, kPadMode = 0x40, kPadBits = 0xC0;  // a pad button on bit 6
constexpr unsigned kMd2 = 6;
constexpr uint8_t kRegPortEnable0 = 0x82, kEnable1F = 0x40, kEnable37 = 0x80, kIdleBus = 0xFF;
}  // namespace

TEST_F(NextAudioInput_Test, TheJoystickPortsAreDrivenByTheModesAndTheirEnableBits)
{
    Joystick* stick = _context->pJoystick;
    ASSERT_NE(stick, nullptr);
    stick->SetState(kStickRight | kStickFire);
    auto in = [&](uint16_t port) { return _ports->DecodePortIn(port, 0); };

    // power on: Kempston 1 on the left answers #1F, nothing answers #37
    EXPECT_EQ(in(kPortKempston1), kStickRight | kStickFire);
    EXPECT_EQ(in(kPortKempston2), kIdleBus);

    // Kempston 2 on the left: #37 answers, #1F idles
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kKempston2));
    EXPECT_EQ(in(kPortKempston2), kStickRight | kStickFire);
    EXPECT_EQ(in(kPortKempston1), kIdleBus);

    // an MD pad passes bits 7:6, a Kempston stick does not
    stick->SetState(kStickRight | kPadMode);
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kMd1));
    EXPECT_EQ(in(kPortKempston1), kStickRight | kPadMode);
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kKempston1));
    EXPECT_EQ(in(kPortKempston1), kStickRight) << "no pad bits on a Kempston stick";
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kMd2));
    EXPECT_EQ(in(kPortKempston2), kStickRight | kPadMode);

    // the host joystick is the left connector: a right side that is Kempston 1 answers the port with nothing pressed on it
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kSinclair2) | RightMode(kKempston1));
    EXPECT_EQ(in(kPortKempston1), 0x00);

    // a port without its bit in NR #82 is not decoded
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kKempston1));
    Board().WriteNextReg(kRegPortEnable0, static_cast<uint8_t>(0xFF & ~kEnable1F));
    EXPECT_EQ(in(kPortKempston1), kIdleBus);
    Board().WriteNextReg(kRegPortEnable0, static_cast<uint8_t>(0xFF & ~kEnable37));
    Board().WriteNextReg(kRegJoystickModes, LeftMode(kKempston2));
    EXPECT_EQ(in(kPortKempston2), kIdleBus);
    (void)kPadBits;
}
