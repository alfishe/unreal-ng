/// @file ttdpage255_test.cpp
/// @brief RAM page 255 is an ordinary page to time travel (PLAN #40 V0).
///
/// Memory's per-bank page cache used to be a uint8_t with 0xFF meaning "this
/// bank holds ROM". On a 4 MB machine (ATM3 today, TSConf later) 0xFF is also
/// the last real RAM page, so writes to page 255 looked like ROM writes: never
/// marked dirty, never journaled, invisible to the write probe, and lumped
/// together with ROM in the coverage index. A restore then showed stale page
/// 255 contents until the next key frame.
///
/// The machine here is ATM3 with page 255 banked into window 3 and a small
/// loop running from it, so every access kind (execute, read, write) touches
/// page 255 while the rest of the machine stays quiet.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdcoverageindex.h"
#include "debugger/ttd/ttddirtytracker.h"
#include "debugger/ttd/ttdprobe.h"
#include "debugger/ttd/ttdwritejournal.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_atm710.h"

namespace
{

constexpr uint16_t kTopPage = 255;          ///< The page that used to be the sentinel
constexpr uint16_t kCounterAddr = 0xC100;   ///< Byte the loop increments (window 3)
constexpr uint16_t kLoopStart = 0xC000;
constexpr uint16_t kStoreInsn = 0xC004;     ///< PC of LD (nn),A

class TTD_Page255_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    uint16_t _romRet = 0;  ///< Address of a RET in the ROM banked at 0x0000

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "ATM3 must be creatable (4 MB, pages 0..255)";

        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        _memory = _context->pMemory;
        _z80 = _context->pCore->GetZ80();
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_memory, nullptr);
        ASSERT_NE(_z80, nullptr);
        ASSERT_GE(MAX_RAM_PAGES, 256) << "test needs a 256-page RAM ceiling";

        FeatureManager* fm = _emulator->GetFeatureManager();
        ASSERT_NE(fm, nullptr);
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
        _emulator->EnableTurboMode();  // no assertion looks at rendered pixels

        InstallLoopInPage255();
    }

    void TearDown() override
    {
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// Bank page 255 into window 3 and park the CPU in a loop there:
    ///
    ///   C000  LD A,(C100)    read    page 255
    ///   C003  INC A
    ///   C004  LD (C100),A    write   page 255
    ///   C007  CALL rom_ret   execute ROM (the "no page" bucket)
    ///   C00A  JR C000
    ///
    /// Interrupts are off, so nothing else runs. Stack is in page 255 too.
    void InstallLoopInPage255()
    {
        // Program the ATM memory manager the way guest code would (PEN on,
        // ~CPM set, register set 0): ROM page 28 (BASIC48, present in every
        // ZX-Evo image - page 0 is the custom-ROM slot, empty in the official
        // zxevo-fe.rom) in window 0, RAM 5 / 2 in windows 1-2 and RAM 255 in
        // window 3 - "RAM from FFF7" (0x200 | page). Setting a bank directly
        // would not stick: every rebank recomputes the windows from these latches.
        EmulatorState& st = _context->emulatorState;
        st.aFF77 |= PortDecoder_ATM710::ATM_AFF77_PEN | PortDecoder_ATM710::ATM_AFF77_CPM;
        st.p7FFD = 0;
        st.pFFF7[0] = 0x300 | 28;
        st.pFFF7[1] = 0x200 | 0x05;
        st.pFFF7[2] = 0x200 | 0x02;
        st.pFFF7[3] = 0x200 | kTopPage;
        _memory->UpdateZ80Banks();
        ASSERT_EQ(_memory->GetRAMPageForBank(3), kTopPage) << "ATM manager did not bank page 255";

        // A RET somewhere in the ROM above offset 0x200, so its coverage key
        // offset cannot coincide with an offset the loop touches in page 255.
        for (uint16_t a = 0x0200; a < 0x3FFF; ++a)
        {
            if (_memory->DirectReadFromZ80Memory(a) == 0xC9)
            {
                _romRet = a;
                break;
            }
        }
        ASSERT_NE(_romRet, 0) << "no RET (0xC9) found in the ROM page at 0x0000";

        uint8_t* page = _memory->RAMPageAddress(kTopPage);
        ASSERT_NE(page, nullptr);
        const std::array<uint8_t, 12> code = {
            0x3A, 0x00, 0xC1,                                                  // LD A,(C100)
            0x3C,                                                              // INC A
            0x32, 0x00, 0xC1,                                                  // LD (C100),A
            0xCD, static_cast<uint8_t>(_romRet), static_cast<uint8_t>(_romRet >> 8),  // CALL rom_ret
            0x18, 0xF4,                                                        // JR C000
        };
        std::memcpy(page, code.data(), code.size());
        page[kCounterAddr & 0x3FFF] = 0;

        _z80->pc = kLoopStart;
        _z80->sp = 0xC800;
        _z80->iff1 = _z80->iff2 = 0;
    }

    void Record(unsigned frames)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_GT(_ttd->GetCheckpointCount(), 1u);
    }

    uint8_t Counter() const { return _memory->RAMPageAddress(kTopPage)[kCounterAddr & 0x3FFF]; }
};

}  // namespace

TEST_F(TTD_Page255_Test, BankCacheReportsPage255AsRam)
{
    EXPECT_EQ(_memory->GetPhysPageForZ80Address(kCounterAddr), kTopPage);
    EXPECT_NE(_memory->GetPhysPageForZ80Address(kCounterAddr), ttd::kPhysPageNone);
    EXPECT_EQ(_memory->GetPhysPageForZ80Address(0x0000), ttd::kPhysPageNone)
        << "window 0 holds ROM and must keep reporting 'no page'";
}

TEST_F(TTD_Page255_Test, WritesToPage255_MarkThePageDirty)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);
    EXPECT_TRUE(tracker->WasEverDirty(kTopPage)) << "page 255 was written but never marked dirty";
    _ttd->StopRecording();
}

TEST_F(TTD_Page255_Test, WritesToPage255_AreJournaled)
{
    _ttd->SetEnableWriteJournal(true);
    Record(2);

    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr);
    auto rec = journal->FindLast(UINT64_MAX, [](const ttd::TTDWriteRecord& r)
                                 { return r.isIo == 0 && r.addr == kCounterAddr; });
    ASSERT_TRUE(rec.has_value()) << "no journal record for the page-255 counter write";
    EXPECT_EQ(rec->physPage, kTopPage);
    EXPECT_EQ(rec->m1pc, kStoreInsn);
}

/// The access probe - what reverse search arms during silent replay - must see
/// page-255 writes. The old sentinel gate in MemoryWriteDebug skipped the probe
/// (and the journal, and dirty tracking) for exactly that page. Armed directly
/// so the test pins the write hook itself, independent of search strategy.
TEST_F(TTD_Page255_Test, WriteProbe_SeesPage255)
{
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounterAddr;
    q.access = ttd::TTDAccessType::Write;
    q.hasPhysPageFilter = true;
    q.physPage = kTopPage;

    _context->ttdProbe.Arm(q);
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    std::vector<ttd::TTDSearchResult> hits = _context->ttdProbe.ExtractHits();
    _context->ttdProbe.Disarm();

    ASSERT_FALSE(hits.empty()) << "probe never saw the page-255 write";
    EXPECT_EQ(hits.back().pc, kStoreInsn);
    EXPECT_EQ(hits.back().physPage, kTopPage);

    q.physPage = 254;
    _context->ttdProbe.Arm(q);
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    hits = _context->ttdProbe.ExtractHits();
    _context->ttdProbe.Disarm();
    EXPECT_TRUE(hits.empty()) << "page filter matched the wrong page";
}

/// Read searches replay through the same probe from the read hook.
TEST_F(TTD_Page255_Test, ReadSearch_FindsPage255)
{
    Record(3);
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounterAddr;
    q.access = ttd::TTDAccessType::Read;
    q.hasPhysPageFilter = true;
    q.physPage = kTopPage;
    auto hit = _ttd->FindLastAccess(q);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->pc, kLoopStart);
    EXPECT_EQ(hit->physPage, kTopPage);
}

TEST_F(TTD_Page255_Test, WriteSearch_FindsPage255_ThroughTheJournal)
{
    _ttd->SetEnableWriteJournal(true);
    Record(3);
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = kCounterAddr;
    q.access = ttd::TTDAccessType::Write;
    q.hasPhysPageFilter = true;
    q.physPage = kTopPage;
    auto hit = _ttd->FindLastAccess(q);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->pc, kStoreInsn);
    EXPECT_EQ(hit->physPage, kTopPage);
}

/// Restoring any delta frame must bring page 255 back exactly as it was.
/// With the page never marked dirty, deltas carried stale contents until the
/// next key frame.
TEST_F(TTD_Page255_Test, SeekRestoresPage255InDeltaFrames)
{
    std::map<uint64_t, std::vector<uint8_t>> snapshots;
    auto snap = [&]()
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1);
        ASSERT_NE(cp, nullptr);
        const uint8_t* p = _memory->RAMPageAddress(kTopPage);
        snapshots[cp->time.frame].assign(p, p + PAGE_SIZE);
    };

    // Whole frames through MainLoop, so each snapshot is taken exactly at the
    // boundary its checkpoint was captured at (RunNFrames counts t-states from
    // wherever the CPU is and would stop a few instructions off).
    MainLoop_CUT mainloop(_context);
    ASSERT_TRUE(_ttd->StartRecording());
    snap();
    for (int f = 0; f < 6; ++f)
    {
        mainloop.RunFramePublic();
        snap();
    }
    _ttd->StopRecording();
    ASSERT_GE(snapshots.size(), 5u);

    // Walk backwards and forwards so both directions are covered.
    std::vector<uint64_t> frames;
    for (const auto& kv : snapshots)
        frames.push_back(kv.first);
    std::vector<uint64_t> order(frames.rbegin(), frames.rend());
    order.insert(order.end(), frames.begin(), frames.end());

    for (uint64_t frame : order)
    {
        ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{frame, 0})) << "seek to frame " << frame;
        const uint8_t* p = _memory->RAMPageAddress(kTopPage);
        EXPECT_EQ(0, std::memcmp(p, snapshots[frame].data(), PAGE_SIZE))
            << "page 255 differs after seeking to frame " << frame
            << " (counter now " << static_cast<int>(Counter()) << ", recorded "
            << static_cast<int>(snapshots[frame][kCounterAddr & 0x3FFF]) << ")";
    }
}

/// ROM execution and page-255 execution must land in different coverage
/// buckets, or a page-255 query is answered with ROM frames (and vice versa).
TEST_F(TTD_Page255_Test, Coverage_SeparatesPage255FromRom)
{
    _ttd->SetEnableCoverageIndex(true);
    Record(3);

    const uint64_t frame = _ttd->GetCheckpoint(1)->time.frame;
    const uint16_t romOffset = _romRet & 0x3FFF;

    // Page 255 executed the loop...
    auto loop = _ttd->QueryCoverageProbe(frame, ttd::TTDCoverageKind::Executed, kLoopStart,
                                         kLoopStart + 11, kTopPage);
    ASSERT_TRUE(loop.indexAvailable);
    EXPECT_TRUE(loop.touched) << "page-255 execution missing from the coverage index";

    // ...but never the offset where the ROM RET lives. Before the fix the ROM
    // fetch was filed under page 0xFF and this answered "touched".
    auto romAsPage255 = _ttd->QueryCoverageProbe(frame, ttd::TTDCoverageKind::Executed,
                                                 0xC000 + romOffset, 0xC000 + romOffset,
                                                 kTopPage);
    ASSERT_TRUE(romAsPage255.indexAvailable);
    EXPECT_FALSE(romAsPage255.touched) << "ROM execution was filed under RAM page 255";

    // Writes: the counter is in page 255, nothing writes page 254.
    auto w255 = _ttd->QueryCoverageProbe(frame, ttd::TTDCoverageKind::Written, kCounterAddr,
                                         kCounterAddr, kTopPage);
    EXPECT_TRUE(w255.touched);
}

/// A session saved and loaded again keeps page 255 distinct from ROM.
TEST_F(TTD_Page255_Test, Coverage_SurvivesSerialization)
{
    _ttd->SetEnableCoverageIndex(true);
    _ttd->SetEnableWriteJournal(true);
    Record(3);
    const uint64_t frame = _ttd->GetCheckpoint(1)->time.frame;
    const uint16_t romOffset = _romRet & 0x3FFF;

    std::stringstream buf;
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(buf, err)) << err;
    buf.seekg(0);
    ASSERT_TRUE(_ttd->DeserializeSession(buf, err)) << err;

    auto loop = _ttd->QueryCoverageProbe(frame, ttd::TTDCoverageKind::Executed, kLoopStart,
                                         kLoopStart + 11, kTopPage);
    ASSERT_TRUE(loop.indexAvailable) << "coverage index lost in the round trip";
    EXPECT_TRUE(loop.touched);
    auto romAsPage255 = _ttd->QueryCoverageProbe(frame, ttd::TTDCoverageKind::Executed,
                                                 0xC000 + romOffset, 0xC000 + romOffset,
                                                 kTopPage);
    EXPECT_FALSE(romAsPage255.touched);

    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr);
    auto rec = journal->FindLast(UINT64_MAX, [](const ttd::TTDWriteRecord& r)
                                 { return r.isIo == 0 && r.addr == kCounterAddr; });
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->physPage, kTopPage);
}
