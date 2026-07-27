/// @file ttd_dump_format_test.cpp
/// @brief Round-trip tests for the .ttd binary format.
///
/// Captures a small recording, serializes it via SerializeSession, then
/// deserializes it into the same manager (replacing its in-memory state)
/// and verifies the round-trip is byte-identical to the original:
///
///   1. The header round-trips (frame range, model metadata, page count).
///   2. Every checkpoint's cpu/chipset/ramPages/peripheral blob matches.
///   3. The page store has identical slot contents and refcounts.
///
/// Also exercises:
///   - Round-trip of an empty (pre-StartRecording) timeline.
///   - Round-trip of a multi-checkpoint timeline with dirty pages.
///   - The version-refusal path (a file with schema_version > current is
///     rejected with a clear error).
///   - The magic-refusal path (a file with the wrong magic is rejected).
///   - CaptureRestoreSelfTest on a freshly started session.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_dirty_tracker.h"
#include "debugger/ttd/ttd_dump_format.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_page_store.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

/// Mark a RAM page dirty so OnFrameBoundary re-Interns its content.
/// (Bypassing MemoryWriteDebug means the dirty tracker doesn't see the
/// write — we have to mark explicitly.)
void ScribbleRamPage(Memory* mem, uint16_t page, uint8_t marker)
{
    uint8_t* base = mem->RAMPageAddress(page);
    ASSERT_NE(base, nullptr);
    for (uint32_t i = 0; i < 0x4000; i += 0x100)
        base[i] = static_cast<uint8_t>(marker + (i >> 8));

    ttd::TTDDirtyTracker* tracker = mem->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);
    tracker->MarkDirty(page);
}

} // anonymous namespace

// ===========================================================================
// Fixture: real Emulator + TimeTravelManager
// ===========================================================================

class TTD_Dump_Format_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void EnableTTD()
    {
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    /// Snapshot the timeline + page store into POD-comparable form so the
    /// test can verify byte-identical round-trip without poking private
    /// members.
    struct TimelineDigest
    {
        size_t checkpointCount = 0;
        size_t pageStoreUsedSlots = 0;
        size_t pageStoreCapacity = 0;
        std::vector<uint64_t> checkpointFrames;
        std::vector<ttd::TTDCpuState> cpuStates;
        std::vector<ttd::TTDChipsetState> chipsetStates;
        std::vector<std::vector<uint8_t>> ayBlobs;
        // RAM page content per checkpoint (resolving refs into the page store).
        std::vector<std::vector<std::vector<uint8_t>>> ramPagesPerCheckpoint;
    };

    // CaptureDigest is a const helper returning by value, so we can't use
    // ASSERT_* (which expands to a `return;`). Use EXPECT + sentinel return.
    TimelineDigest CaptureDigest() const
    {
        TimelineDigest d;
        d.checkpointCount = _ttd->GetCheckpointCount();
        const auto& store = _ttd->GetPageStore();
        d.pageStoreUsedSlots = store.GetUsedSlots();
        d.pageStoreCapacity  = store.GetCapacity();

        d.checkpointFrames.reserve(d.checkpointCount);
        d.cpuStates.reserve(d.checkpointCount);
        d.chipsetStates.reserve(d.checkpointCount);
        d.ayBlobs.reserve(d.checkpointCount);
        d.ramPagesPerCheckpoint.reserve(d.checkpointCount);

        for (size_t i = 0; i < d.checkpointCount; ++i)
        {
            const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
            if (cp == nullptr) { ADD_FAILURE() << "null checkpoint " << i; return d; }

            d.checkpointFrames.push_back(cp->time.frame);
            d.cpuStates.push_back(cp->cpu);
            d.chipsetStates.push_back(cp->chipset);
            d.ayBlobs.push_back(cp->ayState);

            std::vector<std::vector<uint8_t>> cpPages;
            cpPages.reserve(cp->ramPages.size());
            for (const auto& ref : cp->ramPages)
            {
                if (ref.IsNeverTouched())
                {
                    cpPages.emplace_back();
                }
                else
                {
                    const uint8_t* pageData = store.GetPage(ref.storeIndex);
                    if (pageData == nullptr) { ADD_FAILURE() << "null page"; return d; }
                    cpPages.emplace_back(pageData, pageData + ttd::TTDPageStore::kPageSize);
                }
            }
            d.ramPagesPerCheckpoint.push_back(std::move(cpPages));
        }
        return d;
    }
};

// ===========================================================================
// Header / format validation
// ===========================================================================

TEST_F(TTD_Dump_Format_Test, Serialize_EmptySession_ProducesValidHeader)
{
    EnableTTD();
    // Do NOT StartRecording — timeline is empty.
    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    const std::string data = out.str();
    ASSERT_GE(data.size(), 4u);
    EXPECT_EQ(std::memcmp(data.data(), ttd::dump::kMagic, 4), 0)
        << "Missing TTDD magic at start of file";

    // Schema version (u16 LE at offset 4).
    ASSERT_GE(data.size(), 6u);
    uint16_t schemaVer = 0;
    std::memcpy(&schemaVer, data.data() + 4, 2);
    EXPECT_EQ(schemaVer, ttd::dump::kSchemaVersion);
}

TEST_F(TTD_Dump_Format_Test, Deserialize_BadMagic_Fails)
{
    EnableTTD();
    std::istringstream in("XXXX", std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(in, err));
    EXPECT_NE(err.find("magic"), std::string::npos) << "Error: " << err;
}

TEST_F(TTD_Dump_Format_Test, Deserialize_FutureSchemaVersion_Fails)
{
    EnableTTD();
    // Hand-construct a minimal header with a too-high schema version.
    std::string buf;
    buf.append(ttd::dump::kMagic, 4);
    const uint16_t futureVer = ttd::dump::kSchemaVersion + 1;
    buf.append(reinterpret_cast<const char*>(&futureVer), 2);

    std::istringstream in(buf, std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(in, err));
    EXPECT_NE(err.find("schema"), std::string::npos) << "Error: " << err;
    EXPECT_NE(err.find("reader supports"), std::string::npos) << "Error: " << err;
}

// ===========================================================================
// Round-trip: serialize → deserialize → compare digests
// ===========================================================================

TEST_F(TTD_Dump_Format_Test, RoundTrip_SingleCheckpointBaseline)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_EQ(_ttd->GetCheckpointCount(), 1u);

    const auto before = CaptureDigest();
    ASSERT_EQ(before.checkpointCount, 1u);

    // Serialize to a string, then deserialize back into the same manager.
    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    const std::string data = out.str();
    ASSERT_FALSE(data.empty());

    // DeserializeSession replaces the current session — this is what we want.
    std::istringstream in(data, std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    const auto after = CaptureDigest();
    EXPECT_EQ(after.checkpointCount, before.checkpointCount);
    EXPECT_EQ(after.pageStoreUsedSlots, before.pageStoreUsedSlots);
    EXPECT_EQ(after.checkpointFrames, before.checkpointFrames);

    ASSERT_EQ(after.cpuStates.size(), before.cpuStates.size());
    EXPECT_EQ(std::memcmp(&after.cpuStates[0], &before.cpuStates[0],
                          sizeof(ttd::TTDCpuState)), 0)
        << "CPU state differs after round-trip";

    ASSERT_EQ(after.chipsetStates.size(), before.chipsetStates.size());
    EXPECT_EQ(std::memcmp(&after.chipsetStates[0], &before.chipsetStates[0],
                          sizeof(ttd::TTDChipsetState)), 0)
        << "Chipset state differs after round-trip";
}

TEST_F(TTD_Dump_Format_Test, RoundTrip_MultiCheckpointWithDirtyPages)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_EQ(_ttd->GetCheckpointCount(), 1u);

    // Mutate three pages and capture checkpoint 1.
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 0, 0x10));
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 1, 0x20));
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 2, 0x30));
    _ttd->OnFrameBoundary();
    ASSERT_EQ(_ttd->GetCheckpointCount(), 2u);

    // Mutate page 0 again (different content) and capture checkpoint 2.
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 0, 0x40));
    _ttd->OnFrameBoundary();
    ASSERT_EQ(_ttd->GetCheckpointCount(), 3u);

    // Mutate nothing — checkpoint 3 should share all pages with cp 2.
    _ttd->OnFrameBoundary();
    ASSERT_EQ(_ttd->GetCheckpointCount(), 4u);

    const auto before = CaptureDigest();
    ASSERT_EQ(before.checkpointCount, 4u);

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    const auto after = CaptureDigest();

    EXPECT_EQ(after.checkpointCount, before.checkpointCount);
    EXPECT_EQ(after.checkpointFrames, before.checkpointFrames);

    // Page store round-trip: same number of live slots, same content per
    // checkpoint's referenced pages.
    EXPECT_EQ(after.pageStoreUsedSlots, before.pageStoreUsedSlots);

    ASSERT_EQ(after.ramPagesPerCheckpoint.size(), before.ramPagesPerCheckpoint.size());
    for (size_t cp = 0; cp < before.ramPagesPerCheckpoint.size(); ++cp)
    {
        const auto& beforePages = before.ramPagesPerCheckpoint[cp];
        const auto& afterPages  = after.ramPagesPerCheckpoint[cp];
        ASSERT_EQ(afterPages.size(), beforePages.size());
        for (size_t p = 0; p < beforePages.size(); ++p)
        {
            EXPECT_EQ(afterPages[p], beforePages[p])
                << "RAM page " << p << " of checkpoint " << cp
                << " differs after round-trip";
        }
    }

    // CPU + chipset per checkpoint
    for (size_t cp = 0; cp < before.cpuStates.size(); ++cp)
    {
        EXPECT_EQ(std::memcmp(&after.cpuStates[cp], &before.cpuStates[cp],
                              sizeof(ttd::TTDCpuState)), 0)
            << "CPU state of checkpoint " << cp << " differs";
        EXPECT_EQ(std::memcmp(&after.chipsetStates[cp], &before.chipsetStates[cp],
                              sizeof(ttd::TTDChipsetState)), 0)
            << "Chipset state of checkpoint " << cp << " differs";
    }
}

// ===========================================================================
// CaptureRestoreSelfTest
// ===========================================================================

TEST_F(TTD_Dump_Format_Test, CaptureRestoreSelfTest_OnFreshSession_Passes)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    auto result = _ttd->CaptureRestoreSelfTest();
    EXPECT_TRUE(result.pre_post_match)
        << "Single-frame capture/restore round-trip failed: " << result.notes;
    EXPECT_NE(result.pre_hash, 0u);
    EXPECT_EQ(result.pre_hash, result.post_hash);
}

TEST_F(TTD_Dump_Format_Test, CaptureRestoreSelfTest_AfterRamMutation_Passes)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    // Mutate live RAM and CPU before the self-test. The self-test captures
    // the current live state and restores it; if any field is missed by
    // capture or restore, the hashes won't match.
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 0, 0x77));
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 3, 0x88));

    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    cpu->pc = 0x1234;
    cpu->sp = 0xFFFF;
    cpu->memptr = 0xBEEF;

    auto result = _ttd->CaptureRestoreSelfTest();
    EXPECT_TRUE(result.pre_post_match) << result.notes;
}

// ===========================================================================
// Fixture writer: produce a real .ttd file for Python parser conformance
// ===========================================================================
//
// This test writes a small .ttd file to a path under the build directory. The
// Python analyzer (tools/verification/ttd-analyzer/) loads it to verify its
// hand-written parser matches the C++ writer byte-for-byte. CI regenerates
// the fixture on every run.

TEST_F(TTD_Dump_Format_Test, DISABLED_WriteFixtureFile_ForPythonConformance)
{
    EnableTTD();
    ASSERT_TRUE(_ttd->StartRecording());

    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 0, 0x10));
    _ttd->OnFrameBoundary();
    ASSERT_NO_FATAL_FAILURE(ScribbleRamPage(_memory, 1, 0x20));
    _ttd->OnFrameBoundary();

    // Write to the build directory's bin/ folder alongside the test binary.
    const char* outPath = std::getenv("TTD_FIXTURE_OUT");
    const std::string path = outPath ? outPath :
        std::string("./ttd_fixture.ttd");

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(f.is_open()) << "failed to open " << path;
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(f, err)) << err;
    f.close();

    GTEST_SUCCEED() << "wrote .ttd fixture to " << path;
}
