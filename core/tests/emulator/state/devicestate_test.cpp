#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/media/mediamanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/memory/memory.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/screen.h"

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
    SoundCardScope _turboSound{TestSound::TurboSound};  // the slot is the subject
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

/// The +3 reports its own controller, the uPD765A, not the WD1793 every model carries: the command in
/// hand with its parameters, the phase, the status bytes, SPECIFY times, and its two drives
TEST(DeviceStateFdc_Test, Plus3ReportsTheUpd765)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PLUS3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pUPD765, nullptr);
    ASSERT_TRUE(emulator->CreateBlankDisk(0));
    PortDecoder* ports = context->pPortDecoder;

    ports->DecodePortOut(0x1FFD, 0x08, 0x8000);  // motor on
    for (uint8_t byte : { uint8_t(UPD765::CMD_SPECIFY), uint8_t(0xAF), uint8_t(0x03) })
        ports->DecodePortOut(0x3FFD, byte, 0x8000);
    for (uint8_t byte : { uint8_t(uint8_t(UPD765::CMD_READ_DATA) | uint8_t(UPD765::CMD_FLAG_MF)), uint8_t(0x00), uint8_t(0),
                          uint8_t(0), uint8_t(3), uint8_t(2), uint8_t(3), uint8_t(0x2A), uint8_t(0xFF) })
        ports->DecodePortOut(0x3FFD, byte, 0x8000);

    const StateNode fdc = DeviceState::Fdc(context);
    ASSERT_TRUE(At(fdc, "available").b);
    EXPECT_EQ(At(fdc, "controller").s, "uPD765A (+3)");
    EXPECT_EQ(At(fdc, "phase").s, "execution");
    EXPECT_TRUE(At(At(fdc, "main_status"), "execution").b);
    EXPECT_TRUE(At(At(fdc, "main_status"), "busy").b);

    const StateNode& cmd = At(fdc, "command");
    EXPECT_EQ(At(cmd, "name").s, "read_data");
    EXPECT_TRUE(At(cmd, "complete").b);
    EXPECT_TRUE(At(cmd, "mfm").b);
    EXPECT_EQ(At(cmd, "r").i, 3);
    EXPECT_EQ(At(cmd, "n").i, 2);
    EXPECT_EQ(At(cmd, "eot").i, 3);
    EXPECT_EQ(At(cmd, "bytes").size(), 9u);

    EXPECT_EQ(At(At(fdc, "specify"), "step_rate_ms").i, 12);  // SRT #A: (16 - 10) x 2 ms
    EXPECT_EQ(At(At(fdc, "specify"), "head_load_ms").i, 4);   // HLT 1 x 4 ms
    EXPECT_TRUE(At(fdc, "motor_on").b);
    ASSERT_EQ(At(fdc, "units").size(), 4u);
    EXPECT_EQ(At(At(fdc, "units").items[2], "drive").s, "A") << "US1 is not connected: unit 2 is drive A";

    // Drives A and B only; A holds the blank +3 disk
    ASSERT_EQ(At(fdc, "drives").size(), 2u);
    const StateNode& driveA = At(fdc, "drives").items[0];
    EXPECT_TRUE(At(driveA, "inserted").b);
    EXPECT_TRUE(At(driveA, "motor_on").b);
    EXPECT_EQ(At(driveA, "path").s, "<blank>");
    EXPECT_EQ(At(At(driveA, "image"), "cylinders").i, 40);
    EXPECT_EQ(At(At(driveA, "image"), "sides").i, 1);

    const std::string text = DeviceState::ToText(fdc);
    EXPECT_NE(text.find("phase: execution"), std::string::npos) << text;

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// region <Screen reports>

namespace
{
/// Machine of the given model with its screen re-detected from the latches
struct ScreenMachine
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;

    explicit ScreenMachine(const char* model)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (emulator)
            context = emulator->GetContext();
    }
    ~ScreenMachine()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
    void Redetect() { context->pScreen->InitRaster(); }
};

std::vector<int64_t> Pages(const StateNode& n)
{
    std::vector<int64_t> out;
    for (const StateNode& item : n.items)
        out.push_back(item.i);
    return out;
}
}  // namespace

TEST(DeviceStateScreen_Test, ShadowScreenOnEveryMachineButThe48K)
{
    // #7FFD bit 3 selects the second screen (page 7) on every 128K-class machine,
    // clones included - the report used to know only 128K / Pentagon / +3
    const std::pair<const char*, bool> machines[] = {
        {"48K", false}, {"128K", true}, {"PENTAGON", true}, {"SCORPION", true}, {"ATM710", true}, {"PROFI", true}};
    for (const auto& [model, capable] : machines)
    {
        SCOPED_TRACE(model);
        ScreenMachine m(model);
        ASSERT_NE(m.context, nullptr);
        m.context->emulatorState.p7FFD = 0x08;

        const StateNode r = DeviceState::Screen(m.context, false);
        EXPECT_EQ(At(r, "shadow_screen_capable").b, capable);
        EXPECT_EQ(At(r, "active_screen").i, capable ? 1 : 0);
        EXPECT_EQ(At(r, "active_ram_page").i, capable ? 7 : 5);
        // Displayed pages depend on the mode (ATM boots into a hires mode: {3, 7}),
        // but always include the selected video page
        const std::vector<int64_t> pages = Pages(At(r, "active_ram_pages"));
        EXPECT_NE(std::find(pages.begin(), pages.end(), capable ? 7 : 5), pages.end());
    }
}

TEST(DeviceStateScreen_Test, ContentionOnlyOnSinclairMachines)
{
    for (const auto& [model, contended] : {std::pair<const char*, bool>{"48K", true}, {"128K", true}, {"PENTAGON", false}, {"ATM710", false}})
    {
        SCOPED_TRACE(model);
        ScreenMachine m(model);
        ASSERT_NE(m.context, nullptr);
        m.Redetect();
        EXPECT_EQ(At(DeviceState::Screen(m.context, false), "contention").b, contended);
    }
}

TEST(DeviceStateScreen_Test, ModeReportFollowsTheActiveMode)
{
    {
        ScreenMachine m("ATM710");
        ASSERT_NE(m.context, nullptr);
        m.context->emulatorState.pFF77 = FF77_MC | 0x20;  // hires 640x200, INT gate on
        m.context->emulatorState.p7FFD = 0x00;
        m.Redetect();
        const StateNode r = DeviceState::ScreenMode(m.context);
        EXPECT_EQ(At(r, "video_mode").s, "ATMHR");
        EXPECT_EQ(At(r, "resolution").s, "640x200");
        EXPECT_EQ(At(r, "bpp").i, 1);
        EXPECT_EQ(At(r, "attribute_size").s, "8x1 pixels");
        EXPECT_EQ(Pages(At(r, "active_ram_pages")), (std::vector<int64_t>{1, 5})) << "bitmap + attribute planes";
        EXPECT_EQ(At(At(r, "ff77"), "video_mode_bits").i, FF77_MC);
    }
    {
        // ATM 4.50: the mode is the low address byte of the last #FE write (OUT #BE = 640x200), no #FF77
        ScreenMachine m("ATM450");
        ASSERT_NE(m.context, nullptr);
        m.context->emulatorState.aFE = 0xBE;
        m.Redetect();
        const StateNode r = DeviceState::ScreenMode(m.context);
        EXPECT_EQ(At(r, "video_mode").s, "ATMHR");
        EXPECT_EQ(At(r, "resolution").s, "640x200");
        EXPECT_EQ(At(At(r, "afe"), "video_mode_bits").i, 1);
        EXPECT_TRUE(At(At(r, "afe"), "rom_at_0000").b);
        EXPECT_EQ(r.find("ff77"), nullptr) << "the 4.50 board has no #FF77";

        m.context->emulatorState.aFE = 0x9E;  // EGA 320x200x16
        m.Redetect();
        EXPECT_EQ(At(DeviceState::ScreenMode(m.context), "video_mode").s, "ATM16");
    }
    {
        ScreenMachine m("PENTAGON");
        ASSERT_NE(m.context, nullptr);
        m.context->emulatorState.pEFF7 = EFF7_HWMC;
        m.Redetect();
        const StateNode r = DeviceState::ScreenMode(m.context);
        EXPECT_EQ(At(r, "video_mode").s, "PMC");
        EXPECT_EQ(At(At(r, "memory_layout"), "attribute_bytes").i, 6144) << "8x1 attributes at pixel + 0x2000";
        EXPECT_EQ(Pages(At(r, "active_ram_pages")), std::vector<int64_t>{5});
        EXPECT_TRUE(At(At(r, "eff7"), "hwmc_enabled").b);
    }
}

TEST(DeviceStateScreen_Test, VerboseMapsEachScreenIntoZ80Space)
{
    ScreenMachine m("128K");
    ASSERT_NE(m.context, nullptr);
    m.context->pMemory->SetRAMPageToBank3(7);
    const StateNode r = DeviceState::Screen(m.context, true);
    EXPECT_EQ(At(At(r, "screen_0"), "z80_access").s, "0x4000-0x7FFF");
    EXPECT_EQ(At(At(r, "screen_1"), "z80_access").s, "0xC000-0xFFFF");
    EXPECT_EQ(At(At(r, "screen_0"), "contention").s, "active");
    EXPECT_NE(r.find("port_0x7FFD"), nullptr);

    ScreenMachine single("48K");
    ASSERT_NE(single.context, nullptr);
    const StateNode s = DeviceState::Screen(single.context, true);
    EXPECT_NE(s.find("screen"), nullptr);
    EXPECT_EQ(s.find("screen_1"), nullptr);
}

TEST(DeviceStateScreen_Test, FlashFollowsTheFrameCounter)
{
    ScreenMachine m("PENTAGON");
    ASSERT_NE(m.context, nullptr);
    m.context->emulatorState.frame_counter = 0x10 + 3;
    const StateNode r = DeviceState::ScreenFlash(m.context);
    EXPECT_EQ(At(r, "flash_phase").s, "inverted");
    EXPECT_EQ(At(r, "frames_until_toggle").i, 13);
    EXPECT_EQ(At(r, "flash_cycle_position").i, 19);
    EXPECT_DOUBLE_EQ(At(r, "toggle_interval_seconds").d, 16 * 20480 / 1e6) << "Pentagon frame is 20480 us";
    EXPECT_TRUE(At(DeviceState::Screen(m.context, false), "flash_inverted").b);
}

TEST(DeviceStateScreen_Test, AttributesDecodeThePage5CellsOnA48K)
{
    ScreenMachine m("48K");
    ASSERT_NE(m.context, nullptr);
    uint8_t* page5 = m.context->pMemory->RAMPageAddress(5);
    ASSERT_NE(page5, nullptr);
    page5[0x1800 + 0] = 0x38;  // ink=0, paper=7, not bright, not flash
    page5[0x1800 + 1] = 0x78;  // ink=0, paper=7, bright, not flash
    page5[0x1800 + 2] = 0xF8;  // ink=0, paper=7, bright, flash

    const StateNode r = DeviceState::ScreenAttributes(m.context);
    EXPECT_TRUE(At(r, "available").b);
    EXPECT_EQ(At(r, "cols").i, 32);
    EXPECT_EQ(At(r, "rows").i, 24);
    ASSERT_EQ(At(r, "screens").items.size(), 1u) << "48K has one screen only";

    const StateNode& screen0 = At(r, "screens").items[0];
    EXPECT_EQ(At(screen0, "screen").i, 0);
    EXPECT_EQ(At(screen0, "ram_page").i, 5);
    ASSERT_EQ(At(screen0, "cells").items.size(), 768u);

    const StateNode& c0 = At(screen0, "cells").items[0];
    EXPECT_EQ(At(c0, "ink").i, 0);
    EXPECT_EQ(At(c0, "paper").i, 7);
    EXPECT_FALSE(At(c0, "bright").b);
    EXPECT_FALSE(At(c0, "flash").b);

    const StateNode& c1 = At(screen0, "cells").items[1];
    EXPECT_TRUE(At(c1, "bright").b);
    EXPECT_FALSE(At(c1, "flash").b);

    const StateNode& c2 = At(screen0, "cells").items[2];
    EXPECT_TRUE(At(c2, "bright").b);
    EXPECT_TRUE(At(c2, "flash").b);
}

TEST(DeviceStateScreen_Test, AttributesReadBothIndependentPagesOnAShadowCapableMachine)
{
    ScreenMachine m("128K");
    ASSERT_NE(m.context, nullptr);
    uint8_t* page5 = m.context->pMemory->RAMPageAddress(5);
    uint8_t* page7 = m.context->pMemory->RAMPageAddress(7);
    ASSERT_NE(page5, nullptr);
    ASSERT_NE(page7, nullptr);
    page5[0x1800] = 0x07;  // ink=7, paper=0
    page7[0x1800] = 0x38;  // ink=0, paper=7

    // Default (screen == -1): both screens, independently addressable
    const StateNode both = DeviceState::ScreenAttributes(m.context);
    ASSERT_EQ(At(both, "screens").items.size(), 2u);
    EXPECT_EQ(At(At(both, "screens").items[0], "ram_page").i, 5);
    EXPECT_EQ(At(At(both, "screens").items[1], "ram_page").i, 7);
    EXPECT_EQ(At(At(At(both, "screens").items[0], "cells").items[0], "ink").i, 7);
    EXPECT_EQ(At(At(At(both, "screens").items[1], "cells").items[0], "paper").i, 7);

    // Explicit screen selection
    const StateNode screen0 = DeviceState::ScreenAttributes(m.context, 0);
    ASSERT_EQ(At(screen0, "screens").items.size(), 1u);
    EXPECT_EQ(At(At(screen0, "screens").items[0], "ram_page").i, 5);

    const StateNode screen1 = DeviceState::ScreenAttributes(m.context, 1);
    ASSERT_EQ(At(screen1, "screens").items.size(), 1u);
    EXPECT_EQ(At(At(screen1, "screens").items[0], "ram_page").i, 7);
}

TEST(DeviceStateScreen_Test, AttributesRejectShadowScreenOnA48K)
{
    ScreenMachine m("48K");
    ASSERT_NE(m.context, nullptr);
    const StateNode r = DeviceState::ScreenAttributes(m.context, 1);
    EXPECT_FALSE(At(r, "available").b);
}

/// endregion </Screen reports>

/// region <General Sound and Covox (PLAN #20)>

namespace
{
std::vector<int64_t> IntItems(const StateNode* array)
{
    std::vector<int64_t> values;
    if (array)
        for (const StateNode& item : array->items)
            values.push_back(item.i);
    return values;
}
}  // namespace

TEST(DeviceStateGs_Test, UnavailableWithoutACard)
{
    // The test runner leaves the GS slot empty unless a SoundCardScope asks for it
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode gs = DeviceState::Gs(emulator->GetContext());
    ASSERT_NE(gs.find("available"), nullptr);
    EXPECT_FALSE(gs.find("available")->b);
    EXPECT_NE(gs.find("description")->s.find("not fitted"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DeviceStateGs_Test, NeoGsReportFollowsTheMailbox)
{
    SoundCardScope cards(TestSound::GeneralSound);  // Pentagon ships GSType=NGS
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pSoundManager->getGeneralSound(), nullptr);

    context->pPortDecoder->DecodePortOut(0x00B3, 0x5A, 0x0000);  // OUT #B3: data to the card
    const StateNode gs = DeviceState::Gs(context);
    EXPECT_TRUE(gs.find("available")->b);
    EXPECT_EQ(gs.find("implementation")->s, "ngs");
    EXPECT_EQ(gs.find("data_from_host")->i, 0x5A);
    EXPECT_EQ(gs.find("channels")->items.size(), 8u) << "NeoGS has 8 DAC channels";
    ASSERT_NE(gs.find("cpu"), nullptr);
    EXPECT_TRUE(gs.find("cpu")->find("coprocessor")->b);

    const StateNode* neogs = gs.find("neogs");
    ASSERT_NE(neogs, nullptr);
    ASSERT_NE(neogs->find("gscfg0_flags"), nullptr);
    EXPECT_FALSE(neogs->find("gscfg0_flags")->items.empty()) << "ROM/RAM mode is always named";
    ASSERT_NE(neogs->find("dma"), nullptr);
    EXPECT_NE(neogs->find("dma")->find("zx")->find("mode"), nullptr);
    EXPECT_EQ(gs.find("fixed_window_hex"), nullptr) << "the RAM window is opt-in";

    const StateNode withRam = DeviceState::Gs(context, /*ramWindow*/ true);
    ASSERT_NE(withRam.find("fixed_window_hex"), nullptr);
    EXPECT_EQ(withRam.find("fixed_window_hex")->s.size(), 0x4000u * 2);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DeviceStateCovox_Test, PentagonSoundDriveSharesTwoPortsWithBeta128)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    context->emulatorState.flags &= ~CF_TRDOS;
    context->pPortDecoder->DecodePortOut(0x00FB, 0x80, 0x0000);  // SoundDrive right B (= mono Covox)
    const StateNode covox = DeviceState::Covox(context);
    ASSERT_TRUE(covox.find("available")->b);
    EXPECT_EQ(covox.find("fitment")->s, "quad");
    EXPECT_FALSE(covox.find("ports")->items.empty()) << "the Pentagon decoder routes the SoundDrive";
    const std::vector<int64_t> shared = IntItems(covox.find("shared_with_beta128"));
    EXPECT_EQ(shared, (std::vector<int64_t>{0x1F, 0x5F})) << "SoundDrive mode 1 left B / right B";
    ASSERT_NE(covox.find("shared_port_rule"), nullptr);

    const StateNode* channels = covox.find("channels");
    ASSERT_EQ(channels->items.size(), 4u);
    EXPECT_EQ(channels->items[3].find("name")->s, "right_b");
    EXPECT_EQ(channels->items[3].find("latch")->i, 0x80);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DeviceStateCovox_Test, PortsFollowTheModelsDecoder)
{
    // ZX-Evo decodes only the #FB DAC
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode covox = DeviceState::Covox(emulator->GetContext());
    ASSERT_TRUE(covox.find("available")->b);
    const StateNode* ports = covox.find("ports");
    ASSERT_FALSE(ports->items.empty());
    for (const StateNode& p : ports->items)
        EXPECT_EQ(p.find("port")->i & 0xFF, 0xFB);
    EXPECT_TRUE(covox.find("shared_with_beta128")->items.empty());
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DeviceStateCovox_Test, SinclairModelsFitNoCovox)
{
    // The Sinclair decoders route no Covox / SoundDrive port, so their configs
    // do not fit one (SD=0, CovoxFB=0) and the report says it is absent
    for (const char* model : {"48K", "128k", "PLUS2", "PLUS2A", "PLUS3"})
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << model;
        const StateNode covox = DeviceState::Covox(emulator->GetContext());
        EXPECT_FALSE(covox.find("available")->b) << model;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// endregion </General Sound and Covox (PLAN #20)>

/// IDE board report: the scheme, the units, a command in flight; a machine
/// without a board answers "unavailable"
TEST(DeviceStateIde_Test, ReportFollowsTheBoardAndTheCommand)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("devicestate-ide.img");
    {
        std::ofstream out(FileHelper::ToFsPath(image), std::ios::binary);
        out << std::string(256 * 512, '\0');
    }
    MediaSource source;
    source.path = image;
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(context->pMediaManager->Insert("ide0.master", source, options).Ok());

    // READ SECTORS of 2 at LBA 5 through the Nemo ports, one word read
    PortDecoder& ports = *context->pPortDecoder;
    context->emulatorState.flags &= static_cast<uint8_t>(~(CF_DOSPORTS | CF_TRDOS));
    for (const auto& [port, value] : std::vector<std::pair<uint16_t, uint8_t>>{
             {0xD0, 0xE0}, {0x50, 2}, {0x70, 5}, {0x90, 0}, {0xB0, 0}, {0xF0, 0x20}})
        ports.DecodePortOut(port, value, 0);
    ports.DecodePortIn(0x10, 0);

    const StateNode report = DeviceState::Ide(context);
    ASSERT_TRUE(report.find("available")->b);
    EXPECT_EQ(report.find("scheme")->s, "NEMO");
    const StateNode& units = *report.find("units");
    ASSERT_EQ(units.items.size(), 2u);
    const StateNode& master = units.items[0];
    EXPECT_EQ(master.find("kind")->s, "disk");
    EXPECT_EQ(master.find("slot")->s, "ide0.master");
    EXPECT_TRUE(master.find("present")->b);
    const StateNode& command = *master.find("command");
    EXPECT_EQ(command.find("name")->s, "READ SECTORS");
    EXPECT_EQ(command.find("phase")->s, "data in");
    EXPECT_EQ(command.find("buffer_position")->i, 2);
    const StateNode& bits = *master.find("task_file")->find("status_bits");
    EXPECT_NE(std::find_if(bits.items.begin(), bits.items.end(), [](const StateNode& b) { return b.s == "DRQ"; }), bits.items.end());
    EXPECT_FALSE(units.items[1].find("present")->b) << "no slave disk";
    EXPECT_NE(DeviceState::ToText(report).find("READ SECTORS"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(emulator);
    std::remove(image.c_str());

    Emulator* spectrum = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(spectrum, nullptr);
    EXPECT_FALSE(DeviceState::Ide(spectrum->GetContext()).find("available")->b);
    EmulatorTestHelper::CleanupEmulator(spectrum);
}

/// region <MoonSound (PLAN #11, P2-2)>

TEST(DeviceStateMoonSound_Test, UnavailableWithoutTheCard)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    for (const StateNode& report : {DeviceState::MoonSound(emulator->GetContext()),
                                    DeviceState::MoonSoundFm(emulator->GetContext()),
                                    DeviceState::MoonSoundPcm(emulator->GetContext())})
    {
        EXPECT_FALSE(report.find("available")->b);
        EXPECT_NE(report.find("description")->s.find("not fitted"), std::string::npos);
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DeviceStateMoonSound_Test, ReportsFollowTheGuestWrites)
{
    SoundCardScope cards(TestSound::MoonSound);  // Pentagon ships MoonSound=1
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pSoundManager->getMoonSound(), nullptr);
    PortDecoder* ports = context->pPortDecoder;
    // The card listens on the full-decode tap Z80::out() calls before the model decode
    auto out = [&](uint16_t port, uint8_t value) {
        ports->NotifyFullDecodeOut(port, value);
        ports->DecodePortOut(port, value, 0x0000);
    };
    auto fm = [&](uint16_t addrPort, uint8_t reg, uint8_t value) {
        out(addrPort, reg);
        out(static_cast<uint16_t>(addrPort + 1), value);
    };
    auto wave = [&](uint8_t reg, uint8_t value) {
        out(0x007E, reg);
        out(0x007F, value);
    };

    fm(0x00C6, 0x05, 0x03);                 // bank 1 reg 5: NEW + NEW2 (OPL4 mode, wave enabled)
    fm(0x00C4, 0x20, 0x41);                 // channel 0 operator 1 (slot 0): VIB, MULT 1
    fm(0x00C4, 0x40, 0x50);                 // KSL register 1 (3 dB/oct), TL 16 (-12 dB)
    fm(0x00C4, 0x60, 0xF2);                 // AR 15, DR 2
    fm(0x00C4, 0x80, 0x21);                 // SL 2 (-6 dB), RR 1
    fm(0x00C4, 0xE0, 0x03);                 // waveform 3 (pulse sine)
    fm(0x00C4, 0xA0, 0x44);                 // channel 0 F-number low
    fm(0x00C4, 0xB0, 0x20 | (4 << 2) | 1);  // key on, block 4, F-number bits 8-9 = 1
    wave(0x50, (0x10 << 1) | 1);             // slot 0 TL 16 (-6 dB), level direct
    wave(0x08, 0x10);                        // slot 0 wave number 16 (its tone header reloads groups 5..9)
    wave(0xB0, 0x50);                        // slot 0 D1L 5 (-15 dB), after the header
    wave(0x68, 0x83);                        // slot 0 key on, pan 3 (left -9 dB)
    // The chip applies a key-on at its next clock: run a frame, as the machine does
    emulator->RunNFrames(1, /*skipBreakpoints=*/true);

    const StateNode overview = DeviceState::MoonSound(context);
    ASSERT_TRUE(overview.find("available")->b);
    EXPECT_TRUE(overview.find("new_mode")->b);
    EXPECT_TRUE(overview.find("new2_mode")->b);
    EXPECT_EQ(IntItems(overview.find("fm_keyed_channels")), (std::vector<int64_t>{0}));
    EXPECT_EQ(IntItems(overview.find("pcm_keyed_slots")), (std::vector<int64_t>{0}));
    EXPECT_GT(overview.find("wave_memory")->find("rom_bytes")->i, 0);

    const StateNode fmReport = DeviceState::MoonSoundFm(context);
    const StateNode& ch0 = fmReport.find("channels")->items[0];
    EXPECT_TRUE(ch0.find("key_on")->b);
    EXPECT_EQ(ch0.find("fnum")->i, 0x144);
    EXPECT_EQ(ch0.find("block")->i, 4);
    EXPECT_GT(ch0.find("frequency_hz")->d, 0.0);
    EXPECT_EQ(fmReport.find("channels")->items.size(), 18u);
    EXPECT_EQ(ch0.find("algorithm")->s, "fm");
    ASSERT_EQ(ch0.find("operators")->items.size(), 2u);
    const StateNode& op1 = ch0.find("operators")->items[0];
    EXPECT_EQ(op1.find("slot")->i, 0);
    EXPECT_TRUE(op1.find("vibrato")->b);
    EXPECT_EQ(op1.find("multiplier")->d, 1.0);
    EXPECT_EQ(op1.find("ksl_db_per_octave")->d, 3.0);
    EXPECT_EQ(op1.find("total_level_db")->d, -12.0);
    EXPECT_EQ(op1.find("ar")->i, 15);
    EXPECT_EQ(op1.find("sustain_level_db")->i, -6);
    EXPECT_EQ(op1.find("waveform_name")->s, "pulse_sine");
    EXPECT_TRUE(op1.find("key_on")->b);
    EXPECT_NE(op1.find("envelope")->find("phase")->s, "off");
    EXPECT_EQ(fmReport.find("registers_bank0_hex")->s.size(), 512u);

    const StateNode pcmReport = DeviceState::MoonSoundPcm(context);
    EXPECT_TRUE(pcmReport.find("wave_enabled")->b);
    ASSERT_EQ(pcmReport.find("slots")->items.size(), 24u);
    const StateNode& slot0 = pcmReport.find("slots")->items[0];
    EXPECT_TRUE(slot0.find("key_on")->b);
    EXPECT_EQ(slot0.find("wave")->i, 16);
    EXPECT_NE(slot0.find("envelope")->find("phase")->s, "off");
    EXPECT_EQ(slot0.find("total_level_db")->d, -6.0);
    EXPECT_TRUE(slot0.find("level_direct")->b);
    EXPECT_EQ(slot0.find("pan")->i, 3);
    EXPECT_EQ(slot0.find("pan_attenuation")->find("left_db")->d, -9.0);
    EXPECT_EQ(slot0.find("pan_attenuation")->find("right_db")->d, 0.0);
    EXPECT_EQ(slot0.find("envelope")->find("decay_level_db")->i, -15);
    EXPECT_EQ(slot0.find("memory")->s, "rom");
    EXPECT_FALSE(pcmReport.find("slots")->items[1].find("key_on")->b);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </MoonSound (PLAN #11, P2-2)>
