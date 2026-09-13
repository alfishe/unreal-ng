#include "stdafx.h"
#include "pch.h"

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

/// DeviceState reports (FM, AY, FDC): the single source every automation
/// interface renders. These tests pin the content; the interface tests only
/// check that the tree reaches the caller unchanged.

namespace
{
const StateNode& At(const StateNode& n, const char* key)
{
    const StateNode* v = n.find(key);
    EXPECT_NE(v, nullptr) << "missing key " << key;
    static const StateNode null;
    return v ? *v : null;
}
}  // namespace

class DeviceState_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pCore->GetZ80()->tt = 0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    void SetT(uint64_t t) { _context->pCore->GetZ80()->tt = uint32_t(t) << 8; }
};

TEST_F(DeviceState_Test, FmReportFollowsRegistersAndEnvelope)
{
    auto* device = dynamic_cast<SoundChip_TurboSoundFM*>(_context->pSoundManager->getTurboSound());
    ASSERT_NE(device, nullptr) << "standard Pentagon config must ship TurboSound=FM";

    // Chip 0, channel 2: S4 carrier at TL 0, others silent, AR 31, extended
    // mode (reg 27 = 0x40) so S1..S3 take per-slot pitches; key S4 only
    SetT(1000);
    device->portDeviceOutMethod(PORT_FFFD, 0xFA);  // chip 0, FM on
    const uint8_t setup[][2] = {
        {0x27, 0x40},
        {0x32, 0x01}, {0x36, 0x01}, {0x3A, 0x01}, {0x3E, 0x02},
        {0x42, 0x7F}, {0x46, 0x7F}, {0x4A, 0x7F}, {0x4E, 0x00},
        {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},
        {0x82, 0x0F}, {0x86, 0x0F}, {0x8A, 0x0F}, {0x8E, 0x0F},
        {0xB2, 0x07},
        {0xAC, 0x1A}, {0xA8, 0x00}, {0xAD, 0x1A}, {0xA9, 0x00}, {0xAE, 0x1A}, {0xAA, 0x00},
        {0xA6, 0x22}, {0xA2, 0x69},
        {0x28, 0x82},  // key-on ch2, slot S4 only
    };
    for (const auto& [reg, data] : setup)
    {
        device->portDeviceOutMethod(PORT_FFFD, reg);
        device->portDeviceOutMethod(PORT_BFFD, data);
    }
    device->syncTo(20000);  // let the attack run

    const StateNode fm = DeviceState::FmChip(_context, 0);
    EXPECT_TRUE(At(fm, "available").b);
    EXPECT_EQ(At(fm, "chip_type").s, "YM2203");
    EXPECT_EQ(At(fm, "prescaler").i, 6);
    EXPECT_NEAR(At(fm, "fm_sample_rate_hz").d, 3500000.0 / 72.0, 1e-6);
    EXPECT_EQ(At(At(fm, "mode"), "channel3_mode").s, "extended");
    EXPECT_FALSE(At(At(fm, "mode"), "csm").b);

    const StateNode& channels = At(fm, "channels");
    ASSERT_EQ(channels.size(), 3u);
    const StateNode& ch2 = channels.items[2];
    EXPECT_EQ(At(ch2, "key_on_mask").i, 0x8);
    EXPECT_TRUE(At(ch2, "key_on").b);
    EXPECT_EQ(At(ch2, "algorithm").i, 7);
    EXPECT_EQ(At(ch2, "fnum").i, 0x269);
    EXPECT_EQ(At(ch2, "block").i, 4);
    EXPECT_NEAR(At(ch2, "frequency_hz").d, 617.0 * 8.0 * (3500000.0 / 72.0) / 1048576.0, 0.01);
    EXPECT_TRUE(At(ch2, "sounding").b);

    const StateNode& ops = At(ch2, "operators");
    ASSERT_EQ(ops.size(), 4u);
    bool sawS4 = false;
    for (const StateNode& op : ops.items)
    {
        const std::string slot = At(op, "slot").s;
        if (slot == "S4")
        {
            sawS4 = true;
            EXPECT_EQ(At(op, "total_level").i, 0);
            EXPECT_EQ(At(op, "multiple").i, 2);
            EXPECT_TRUE(At(op, "key_on").b);
            EXPECT_NE(At(op, "envelope_state").s, "release");
            EXPECT_LT(At(op, "attenuation").i, 0x3FF);
            EXPECT_NEAR(At(op, "frequency_hz").d, 2.0 * At(ch2, "frequency_hz").d, 0.01);  // MUL 2
        }
        else
        {
            EXPECT_EQ(At(op, "total_level").i, 0x7F);
            EXPECT_FALSE(At(op, "key_on").b);
            // extended mode: per-slot pitch (block 3, fnum 0x200)
            EXPECT_EQ(At(op, "block").i, 3);
            EXPECT_EQ(At(op, "fnum").i, 0x200);
        }
    }
    EXPECT_TRUE(sawS4);
    EXPECT_EQ(At(fm, "keyed_channels").i, 1);
    EXPECT_EQ(At(fm, "sounding_channels").i, 1);

    // Overview carries the board latches and the per-chip summary
    const StateNode over = DeviceState::Fm(_context);
    EXPECT_TRUE(At(over, "available").b);
    EXPECT_EQ(At(At(over, "board"), "selected_chip").i, 0);
    EXPECT_TRUE(At(At(over, "board"), "fm_enabled").b);
    ASSERT_EQ(At(over, "chips").size(), 2u);
    EXPECT_EQ(At(At(over, "chips").items[0], "keyed_channels").i, 1);
    EXPECT_EQ(At(At(over, "chips").items[1], "keyed_channels").i, 0);

    EXPECT_FALSE(At(DeviceState::FmChip(_context, 5), "available").b);

    // Text rendering is line based and carries the nested keys
    const std::string text = DeviceState::ToText(fm);
    EXPECT_NE(text.find("chip_type: YM2203"), std::string::npos);
    EXPECT_NE(text.find("channel3_mode: extended"), std::string::npos);
    EXPECT_NE(text.find("slot: S4"), std::string::npos);
}

TEST_F(DeviceState_Test, AyReportDecodesRegisters)
{
    ITurboSoundDevice* ts = _context->pSoundManager->getTurboSound();
    ASSERT_NE(ts, nullptr);
    SetT(1000);
    ts->portDeviceOutMethod(PORT_FFFD, 0xFE);  // chip 0, register mode
    const uint8_t setup[][2] = {{0, 109}, {1, 0}, {7, 0b00111110}, {8, 15}};
    for (const auto& [reg, data] : setup)
    {
        ts->portDeviceOutMethod(PORT_FFFD, reg);
        ts->portDeviceOutMethod(PORT_BFFD, data);
    }

    const StateNode ay = DeviceState::AyChip(_context, 0);
    EXPECT_TRUE(At(ay, "available").b);
    EXPECT_EQ(At(ay, "chip_index").i, 0);
    const StateNode& a = At(ay, "channels").items[0];
    EXPECT_EQ(At(a, "period").i, 109);
    EXPECT_TRUE(At(a, "tone_enabled").b);
    EXPECT_EQ(At(a, "volume").i, 15);
    EXPECT_NEAR(At(a, "frequency_hz").d, 1750000.0 / (16.0 * 110), 1e-6);
    EXPECT_TRUE(At(At(ay, "mixer"), "channel_a_tone").b);
    EXPECT_FALSE(At(At(ay, "mixer"), "channel_b_tone").b);
    EXPECT_EQ(At(At(ay, "registers"), SoundChip_AY8910::AYRegisterNames[8]).i, 15);

    const StateNode over = DeviceState::Ay(_context);
    EXPECT_EQ(At(over, "available_chips").i, 2);
    EXPECT_TRUE(At(over, "turbo_sound").b);
    EXPECT_EQ(At(over, "slot_device").s, "TSFM");
    EXPECT_TRUE(At(At(over, "chips").items[0], "active_channels").b);
    EXPECT_FALSE(At(DeviceState::AyChip(_context, 7), "available").b);
}

TEST_F(DeviceState_Test, FdcReportListsControllerAndDrives)
{
    const StateNode fdc = DeviceState::Fdc(_context);
    ASSERT_TRUE(At(fdc, "available").b) << "Pentagon has a Beta Disk interface";
    EXPECT_EQ(At(fdc, "controller").s, "WD1793 (Beta Disk)");
    EXPECT_EQ(At(fdc, "fsm_state").s, "S_IDLE");
    const StateNode& regs = At(fdc, "registers");
    EXPECT_NE(regs.find("status"), nullptr);
    EXPECT_NE(regs.find("track"), nullptr);
    EXPECT_NE(At(fdc, "status_bits").find("busy"), nullptr);
    EXPECT_NE(At(fdc, "signals").find("intrq"), nullptr);
    ASSERT_EQ(At(fdc, "drives").size(), 4u);
    EXPECT_EQ(At(At(fdc, "drives").items[0], "letter").s, "A");
    // Fresh, untouched drives report a defined state, never leftovers
    for (const StateNode& drive : At(fdc, "drives").items)
    {
        EXPECT_TRUE(At(drive, "present").b);
        EXPECT_FALSE(At(drive, "inserted").b);
        EXPECT_EQ(At(drive, "track").i, 0) << "drive " << At(drive, "letter").s;
        EXPECT_EQ(At(drive, "side").i, 0);
        EXPECT_FALSE(At(drive, "motor_on").b);
        EXPECT_FALSE(At(drive, "write_protected").b);
        EXPECT_EQ(At(drive, "path").s, "");
    }
    EXPECT_EQ(At(fdc, "selected_drive").i, 0);
    EXPECT_EQ(At(fdc, "side").i, 0);
    EXPECT_EQ(At(At(fdc, "registers"), "track").i, 0);
    EXPECT_EQ(At(At(fdc, "registers"), "sector").i, 1);
    EXPECT_FALSE(At(At(fdc, "signals"), "drq").b);
    EXPECT_FALSE(At(At(fdc, "signals"), "intrq").b);
    EXPECT_GE(At(fdc, "selected_drive").i, 0);
    const std::string text = DeviceState::ToText(fdc);
    EXPECT_NE(text.find("fsm_state: S_IDLE"), std::string::npos);
    EXPECT_NE(text.find("drives:"), std::string::npos);
}
