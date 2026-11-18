// Pentagon 2.666 LE and Spectrum snapshots (design section 8): a .sna / .z80 loads into the board as a Pentagon 128
// (PortDecoderP2666::EnterSpectrum128Paging), the clock it ran at stays, and a snapshot is taken only while the board
// is that Pentagon 128.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/snapshotcapture.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
constexpr uint16_t kTurbo0 = 0x1000;  ///< GetClockSelect: #EFF7 bit 4, 3.5 MHz while #xx77 bit 3 is off

Emulator* Create(const char* model)
{
    return EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
}

/// The ZX screen the program shows (page 5 or 7 by #7FFD bit 3), pixels and attributes
std::string ZxScreen(EmulatorContext& context)
{
    const uint8_t page = (context.emulatorState.p7FFD & 0x08) ? 7 : 5;
    const uint8_t* bytes = context.pMemory->RAMPageAddress(page);
    return bytes ? std::string(reinterpret_cast<const char*>(bytes), 6912) : std::string();
}

/// The page the decoder maps in window `w` now
int WindowPageOf(EmulatorContext& context, size_t w)
{
    return static_cast<PortDecoderP2666*>(context.pPortDecoder)->WindowPage(static_cast<uint8_t>(w));
}
}  // namespace

/// The board leaves the reset in Gluk (DOS on, ROM 0 = Gluk). After a load it is a Pentagon 128: ROM at #0000 (the
/// BASIC the file names), RAM 5 and 2, the #7FFD page on top, DOS off, the ZX picture, #EFF7 in 128 mode; the static
/// programs show the Pentagon's screen
TEST(P2666Snapshot_Test, ASpectrumSnapshotRunsOnTheBoardAsAPentagon128)
{
    for (const char* file : {"loaders/sna/across-the-edge-second.sna", "loaders/sna/z80full.sna", "loaders/z80/dizzyx.z80"})
    {
        SCOPED_TRACE(file);
        const std::string path = TestPathHelper::GetTestDataPath(file);
        Emulator* pentagon = Create("PENTAGON");
        Emulator* board = Create("P2666");
        ASSERT_TRUE(pentagon && board);
        EmulatorContext& p = *pentagon->GetContext();
        EmulatorContext& b = *board->GetContext();
        b.pPortDecoder->RestoreClockSelect(kTurbo0);  // the Pentagon's clock, so both run the same instructions

        ASSERT_TRUE(pentagon->LoadSnapshot(path));
        ASSERT_TRUE(board->LoadSnapshot(path));
        const EmulatorState& state = b.emulatorState;
        EXPECT_EQ(state.p7FFD, p.emulatorState.p7FFD);
        EXPECT_FALSE(state.flags & CF_TRDOS) << "DOS off";
        EXPECT_EQ(state.pEFF7, 0x14) << "128 mode, turbo0 kept";
        EXPECT_EQ(state.hw_turbo_ratio, 1u);
        EXPECT_EQ(state.p2666.pxx77 & 0x0F, 0x03) << "the ZX picture, no 28 MHz";
        EXPECT_TRUE(b.pMemory->IsBank0ROM());
        EXPECT_EQ(b.pMemory->GetRAMPageForBank1(), 5);
        EXPECT_EQ(b.pMemory->GetRAMPageForBank2(), 2);
        EXPECT_EQ(b.pMemory->GetRAMPageForBank3(), p.pMemory->GetRAMPageForBank3());
        EXPECT_EQ(b.pScreen->GetVideoMode(), M_PENTAGON128K);
        EXPECT_EQ(std::memcmp(b.pMemory->ROMPageHostAddress(b.pMemory->GetROMPage()),
                              p.pMemory->ROMPageHostAddress(p.pMemory->GetROMPage()), 0x4000),
                  0)
            << "the BASIC the file names";

        pentagon->RunNFrames(3, true);
        board->RunNFrames(3, true);
        EXPECT_TRUE(ZxScreen(b) == ZxScreen(p)) << "the program shows what it shows on a Pentagon";

        EmulatorTestHelper::CleanupEmulator(pentagon);
        EmulatorTestHelper::CleanupEmulator(board);
    }
}

/// A snapshot has no clock: the program runs at the clock the board ran at (owner rule 2026-10-18), here the 28 MHz the
/// board powers on with and the 3.5 MHz of turbo0
TEST(P2666Snapshot_Test, TheClockTheBoardRanAtIsKept)
{
    for (const uint16_t select : {uint16_t(0x0008), kTurbo0})
    {
        Emulator* board = Create("P2666");
        ASSERT_NE(board, nullptr);
        EmulatorContext& b = *board->GetContext();
        b.pPortDecoder->RestoreClockSelect(select);
        const uint8_t ratio = b.emulatorState.hw_turbo_ratio;
        EXPECT_EQ(ratio, select == kTurbo0 ? 1u : 8u);
        ASSERT_TRUE(board->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/across-the-edge-second.sna")));
        EXPECT_EQ(b.emulatorState.hw_turbo_ratio, ratio);
        EmulatorTestHelper::CleanupEmulator(board);
    }
}

/// .sna / .z80 capture: refused while the board runs Gluk (#EFF7 bit 2 off) or an ATM picture, a Pentagon 128 view
/// after a snapshot load
TEST(P2666Snapshot_Test, ASnapshotIsTakenOnlyFromThePentagon128)
{
    Emulator* board = Create("P2666");
    ASSERT_NE(board, nullptr);
    EmulatorContext& b = *board->GetContext();

    const snapshot::MachineView gluk = snapshot::ExamineView(b);
    EXPECT_FALSE(gluk.available);
    EXPECT_EQ(gluk.needs, "mode:pentagon128");
    EXPECT_NE(gluk.reason.find("#EFF7 bit 2"), std::string::npos) << gluk.reason;

    ASSERT_TRUE(board->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/across-the-edge-second.sna")));
    const snapshot::MachineView loaded = snapshot::ExamineView(b);
    ASSERT_TRUE(loaded.available) << loaded.reason;
    EXPECT_EQ(loaded.model, MM_PENTAGON);
    EXPECT_EQ(loaded.machineHint, "pentagon128");
    EXPECT_EQ(loaded.timingHint, "pentagon");
    EXPECT_EQ(loaded.banks.size(), 8u);

    b.emulatorState.p2666.pxx77 = static_cast<uint8_t>((b.emulatorState.p2666.pxx77 & 0xF8) | 0x02);  // ATM hi-res
    const snapshot::MachineView atm = snapshot::ExamineView(b);
    EXPECT_FALSE(atm.available);
    EXPECT_NE(atm.reason.find("ATM video mode"), std::string::npos) << atm.reason;
    EmulatorTestHelper::CleanupEmulator(board);
}

/// The `p2666` node of the paging report (DeviceState::P2666Paging; WebAPI / CLI / Lua / Python / MCP): Gluk at power-on
/// (DOS, 28 MHz, dispatcher windows), a Pentagon 128 after a snapshot load; another model answers available = false
TEST(P2666Snapshot_Test, ThePagingNodeShowsTheBoard)
{
    Emulator* board = Create("P2666");
    ASSERT_NE(board, nullptr);
    EmulatorContext& b = *board->GetContext();

    StateNode gluk = DeviceState::P2666Paging(&b);
    ASSERT_TRUE(gluk.find("available") && gluk.find("available")->b);
    EXPECT_TRUE(gluk["dos"].b);
    EXPECT_EQ(gluk["cpu_mhz"].s, "28");
    EXPECT_EQ(gluk["port_xx77"]["video_mode"].s, "zx");
    EXPECT_EQ(gluk["windows"].items.size(), 4u);
    EXPECT_EQ(gluk["palette"].items.size(), 16u);

    ASSERT_TRUE(board->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/across-the-edge-second.sna")));
    StateNode loaded = DeviceState::P2666Paging(&b);
    EXPECT_FALSE(loaded["dos"].b);
    EXPECT_TRUE(loaded["port_eff7"]["mode128"].b);
    for (size_t w = 0; w < 4; w++)
    {
        EXPECT_EQ(loaded["windows"].items[w]["kind"].s, "pentagon") << w;
        EXPECT_EQ(loaded["windows"].items[w]["page"].i, static_cast<int64_t>(WindowPageOf(b, w))) << w;
    }
    EXPECT_EQ(loaded["windows"].items[3]["page"].i, 7) << "the file's #7FFD = #17";
    EmulatorTestHelper::CleanupEmulator(board);

    Emulator* pentagon = Create("PENTAGON");
    ASSERT_NE(pentagon, nullptr);
    StateNode other = DeviceState::P2666Paging(pentagon->GetContext());
    EXPECT_FALSE(other["available"].b);
    EmulatorTestHelper::CleanupEmulator(pentagon);
}

/// A 48K .z80 locks #7FFD (#30). The board maps a locked #C000 to page 32 + bank (D-3, HR §2.5): the load puts the
/// state's bank there, so the program sees at #4000-#FFFF what it sees on a Pentagon
TEST(P2666Snapshot_Test, ALockedStateSeesItsBankInPage32)
{
    const std::string path = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    Emulator* pentagon = Create("PENTAGON");
    Emulator* board = Create("P2666");
    ASSERT_TRUE(pentagon && board);
    EmulatorContext& p = *pentagon->GetContext();
    EmulatorContext& b = *board->GetContext();
    ASSERT_TRUE(pentagon->LoadSnapshot(path));
    ASSERT_TRUE(board->LoadSnapshot(path));

    EXPECT_EQ(b.emulatorState.p7FFD, 0x30);
    EXPECT_EQ(b.pMemory->GetRAMPageForBank3(), 32) << "the board's D-3";
    int firstDifference = -1;
    for (uint32_t address = 0x4000; address < 0x10000 && firstDifference < 0; address++)
    {
        const auto a16 = static_cast<uint16_t>(address);
        if (b.pMemory->DirectReadFromZ80Memory(a16) != p.pMemory->DirectReadFromZ80Memory(a16))
            firstDifference = static_cast<int>(address);
    }
    EXPECT_EQ(firstDifference, -1);
    EmulatorTestHelper::CleanupEmulator(pentagon);
    EmulatorTestHelper::CleanupEmulator(board);
}
