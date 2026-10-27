/// @file ttd_model_page_bounds_test.cpp
/// @brief The captured RAM page range must cover every page a model banks in.
///
/// TTD walks RAM pages [0, ResolveModelRamPages()) when it captures a
/// checkpoint. That upper limit is a page-index BOUND, not a page count, and
/// the difference is invisible on any model that numbers its pages 0..N-1.
///
/// The 48K machine does not: it owns three pages but Memory maps them as pages
/// 5 (screen), 2 and 0. Deriving the bound from ramsize/16 gave 3, so capture
/// walked pages 0..2 and never saw the display — a recording that restores
/// correct registers into a blank screen.
///
/// Rather than hard-code the expected number per model, these tests assert the
/// invariant that actually matters: whatever page the machine has banked in
/// right now must fall inside the captured range. That keeps holding when new
/// models arrive with page sets nobody has written down yet.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdprobe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

/// Models this build can actually instantiate. PortDecoder::
/// GetPortDecoderForModel throws for anything it has no decoder for, so the
/// list is deliberately short — see the "blocked on machine-model work" note in
/// docs/inprogress/2026-08-20-ttd-reverse-search-index/README.md.
const std::vector<std::string>& CreatableModels()
{
    static const std::vector<std::string> models = {"48K", "128k", "PENTAGON", "SCORPION", "PROFI", "ATM710", "ATM3", "TSL"};
    return models;
}

/// Highest physical RAM page currently mapped into any of the four Z80 banks.
/// Banks holding ROM or cache report kPhysPageNone and are skipped.
int HighestBankedRamPage(Memory* memory)
{
    int highest = -1;
    for (uint32_t bank = 0; bank < 4; ++bank)
    {
        const uint16_t z80Address = static_cast<uint16_t>(bank * 0x4000);
        const ttd::PhysPage page = memory->GetPhysPageForZ80Address(z80Address);
        if (page != ttd::kPhysPageNone && static_cast<int>(page) > highest)
            highest = static_cast<int>(page);
    }
    return highest;
}

class TTD_ModelPageBounds_Test : public ::testing::TestWithParam<std::string>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "cannot create model " << GetParam();

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelController;
        _memory = _context->pMemory;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_memory, nullptr);

        FeatureManager* fm = _emulator->GetFeatureManager();
        ASSERT_NE(fm, nullptr);
        fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
};

}  // namespace

/// The bound must reach past every page the machine has banked in, or capture
/// silently drops live memory.
TEST_P(TTD_ModelPageBounds_Test, BoundCoversEveryBankedPage)
{
    // ATM machines come out of reset with the memory manager off and all four
    // windows on ROM; their ROM enables RAM within the first frames.
    int highest = HighestBankedRamPage(_memory);
    if (highest < 0)
    {
        _emulator->EnableTurboMode();
        for (int frame = 0; frame < 150 && highest < 0; ++frame)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            highest = HighestBankedRamPage(_memory);
        }
    }
    ASSERT_GE(highest, 0) << "no RAM banked in at all on model " << GetParam();

    ASSERT_TRUE(_ttd->StartRecording());

    const uint16_t bound = _ttd->GetModelRamPages();
    EXPECT_GT(bound, static_cast<uint16_t>(highest))
        << "model " << GetParam() << " has page " << highest
        << " banked in, but capture only walks pages 0.." << (bound - 1)
        << " — that page is never recorded";
}

/// Sanity rails on the bound itself: it has to be usable and within the
/// emulator-wide ceiling, which is what sizes every fixed buffer downstream.
TEST_P(TTD_ModelPageBounds_Test, BoundIsWithinTheEmulatorCeiling)
{
    ASSERT_TRUE(_ttd->StartRecording());

    const uint16_t bound = _ttd->GetModelRamPages();
    EXPECT_GT(bound, 0u) << "a zero bound captures no memory at all";
    EXPECT_LE(bound, MAX_RAM_PAGES)
        << "bound exceeds the emulator-wide page ceiling; RAMPageAddress would "
        << "return null for the excess and capture would skip it";
}

/// The checkpoint has to agree with the bound — a mismatch would make restore
/// and serialization disagree about how many refs a checkpoint carries.
TEST_P(TTD_ModelPageBounds_Test, TheRamRegionCoversThePagesInBound)
{
    // The engine keeps machine RAM as one region of 4 KB pieces: it has to agree with the bound, or restore and
    // the session file would disagree about how much RAM a checkpoint holds
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_GE(_ttd->GetCheckpointCount(), 1u);

    const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    const ttd::TTDRegionDesc* ram = nullptr;
    for (const ttd::TTDRegionDesc& r : engine.Regions())
        if (r.name == "ram")
            ram = &r;
    ASSERT_NE(ram, nullptr);
    EXPECT_EQ(ram->pieces, static_cast<uint32_t>(_ttd->GetModelRamPages()) * (PAGE_SIZE / ttd::kTTDPieceSize))
        << "the RAM region does not match the captured page range";
}

/// Every TTD consumer (dirty tracking, write journal, probe, coverage) reads
/// the physical page of an access from Memory's per-bank page cache, never
/// from the bank itself. So the cache must be exact on every bank switch
/// path, for every model: a RAM bank reports the page actually mapped, a ROM
/// bank reports kPhysPageNone. A stale entry after a RAM->ROM switch made TTD
/// journal trash-page writes as writes to the old RAM page and file ROM
/// execution under it.
TEST_P(TTD_ModelPageBounds_Test, BankPageCacheAgreesWithTheMappedBank)
{
    uint8_t* ramBase = _memory->RAMPageAddress(0);
    ASSERT_NE(ramBase, nullptr);

    auto check = [&](int frame)
    {
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint16_t addr = static_cast<uint16_t>(bank * 0x4000);
            const ttd::PhysPage cached = _memory->GetPhysPageForZ80Address(addr);
            if (_memory->GetMemoryBankMode(bank) == BANK_RAM)
            {
                const ptrdiff_t offset = _memory->MapZ80AddressToPhysicalAddress(addr) - ramBase;
                ASSERT_GE(offset, 0);
                const auto mapped = static_cast<ttd::PhysPage>(offset / PAGE_SIZE);
                ASSERT_EQ(cached, mapped) << GetParam() << " frame " << frame << " bank " << int(bank)
                                          << ": page cache disagrees with the RAM page mapped";
            }
            else
            {
                ASSERT_EQ(cached, ttd::kPhysPageNone) << GetParam() << " frame " << frame << " bank " << int(bank)
                                                      << ": non-RAM bank reports RAM page " << cached;
            }
        }
    };

    // No assertion here looks at pixels; turbo only drops host-side work.
    _emulator->EnableTurboMode();

    check(0);
    // Boot long enough for each ROM to rebank (ATM3's BaseConf switches its
    // memory manager during the first seconds). 40-140 ms per model (ATM3 is
    // the slow one): the ROMs have to actually run for their paging code to
    // execute, which is the whole point of the test.
    for (int frame = 1; frame <= 150; ++frame)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        check(frame);
        if (HasFatalFailure())
            return;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AcrossModels, TTD_ModelPageBounds_Test,
    ::testing::ValuesIn(CreatableModels()),
    [](const ::testing::TestParamInfo<std::string>& info)
    {
        std::string name = info.param;
        for (char& c : name)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return name;
    });
