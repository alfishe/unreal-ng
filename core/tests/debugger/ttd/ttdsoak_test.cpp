/// @file ttdsoak_test.cpp
/// @brief The P1.S10 soak's TSL scenario, in the process and on demand: a recording folder with a 64 MiB history
/// limit (the disk tier evicts), branch / switch / resume-at-the-end-of-the-trunk cycles, random seeks with the seek
/// check on, revisits of earlier positions and dumps loaded into a fresh instance. Every check counts; the counts go
/// to stdout.
///
/// Skipped unless UNREAL_TTD_SOAK_MINUTES is set (the wall-clock budget per model, docs/emulator/environment-variables.md):
/// a soak is no unit test. UNREAL_TTD_SOAK_SEED picks the random sequence (default 1).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace fs = std::filesystem;

namespace
{
double SoakMinutes()
{
    const char* v = std::getenv("UNREAL_TTD_SOAK_MINUTES");
    return v ? std::atof(v) : 0.0;
}

uint32_t SoakSeed()
{
    const char* v = std::getenv("UNREAL_TTD_SOAK_SEED");
    return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : 1u;
}

/// One machine with the TTD on
struct SoakMachine
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    bool Create(const std::string& model)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (!emulator)
            return false;
        context = emulator->GetContext();
        ttd = context->pTimeTravelController;
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        return ttd != nullptr;
    }

    ~SoakMachine()
    {
        if (emulator)
        {
            ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }

    /// The state in parts (RAM per 16 KB page, CPU, chipset, each device blob), named by PartNames
    std::vector<uint64_t> StateParts() const
    {
        std::vector<uint64_t> parts;
        const Z80* z80 = context->pCore->GetZ80();
        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*z80);
        const ttd::TTDChipsetState chipset = ttd::CaptureChipsetState(context->emulatorState, static_cast<uint32_t>(z80->t));
        for (uint32_t page = 0; page < ttd->GetModelRamPages(); ++page)
            parts.push_back(ttd::HashBytes(context->pMemory->RAMPageAddress(page), 0x4000));
        parts.push_back(ttd::HashBytes(reinterpret_cast<const uint8_t*>(&cpu), sizeof(cpu)));
        parts.push_back(ttd::HashBytes(reinterpret_cast<const uint8_t*>(&chipset), sizeof(chipset)));
        std::vector<uint8_t> blob;
        for (const ttd::TTDRegistryEntry& e : ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0)
            {
                blob.clear();
                e.device->TTDSaveStateTo(blob);
                parts.push_back(ttd::HashBytes(blob.data(), blob.size()));
            }
        return parts;
    }

    std::vector<std::string> PartNames() const
    {
        std::vector<std::string> names;
        for (uint32_t page = 0; page < ttd->GetModelRamPages(); ++page)
            names.push_back("ram page " + std::to_string(page));
        names.push_back("cpu");
        names.push_back("chipset");
        for (const ttd::TTDRegistryEntry& e : ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0)
                names.push_back("device " + e.key.instance);
        return names;
    }

    /// CPU, chipset, all RAM, every device blob: what a seek has to give back
    uint64_t StateHash() const
    {
        const Z80* z80 = context->pCore->GetZ80();
        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*z80);
        const ttd::TTDChipsetState chipset = ttd::CaptureChipsetState(context->emulatorState, static_cast<uint32_t>(z80->t));
        uint64_t h = ttd::HashBytes(context->pMemory->RAMPageAddress(0), size_t(ttd->GetModelRamPages()) * 0x4000);
        h = h * 31 + ttd::HashBytes(reinterpret_cast<const uint8_t*>(&cpu), sizeof(cpu));
        h = h * 31 + ttd::HashBytes(reinterpret_cast<const uint8_t*>(&chipset), sizeof(chipset));
        std::vector<uint8_t> blob;
        for (const ttd::TTDRegistryEntry& e : ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0)
            {
                blob.clear();
                e.device->TTDSaveStateTo(blob);
                h = h * 31 + ttd::HashBytes(blob.data(), blob.size());
            }
        return h;
    }
};

struct SoakCounts
{
    uint64_t cycles = 0, framesRun = 0, seeks = 0, seeksInFrame = 0, seekFailures = 0;
    uint64_t seekChecks = 0, seekCheckDivergences = 0;
    uint64_t revisits = 0, revisitMismatches = 0;
    uint64_t branchesCreated = 0, switches = 0, switchFailures = 0, trunkEndResumes = 0, trunkEndResumeFailures = 0;
    uint64_t resumeCutTrunk = 0, prunes = 0;
    uint64_t clockSlowed = 0, clockRaised = 0;   ///< trunk frames that ended on a slower / faster CPU clock
    uint64_t dumps = 0, dumpFailures = 0, loads = 0, loadFailures = 0, loadCompares = 0, loadMismatches = 0;
    std::vector<std::string> notes;   ///< the first divergences and mismatches, in full
    void Note(const std::string& s)
    {
        if (notes.size() < 20)
            notes.push_back(s);
    }
};

/// The Pentagon's workload: the two-tier test's loop, over the screen (every frame changes RAM and the picture)
void StartPentagonProgram(SoakMachine& m)
{
    static const uint8_t program[] = {
        0xF3, 0x21, 0x00, 0x40, 0xDB, 0xFE, 0x86, 0x77, 0x23, 0x7C, 0xE6, 0x1F, 0xF6, 0x40, 0x67, 0x18, 0xF3,
    };
    m.emulator->RunNFrames(5);
    Z80* z80 = m.context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;
}

using PosKey = std::tuple<uint32_t, uint64_t, uint32_t>;   ///< branch, frame, T in the frame

/// The positions of @p branch's lineage that can be sought: [the session start, its end]
std::pair<uint64_t, uint64_t> SeekRange(const SoakMachine& m, uint32_t branch)
{
    const ttd::TTDBranchInfo* b = m.ttd->Branches().Get(branch);
    const ttd::TTDSessionInfo info = m.ttd->GetSessionInfo();
    const uint64_t last = b ? b->end.frame : info.currentEndFrame;
    return {info.sessionStartFrame, last > 0 ? last - 1 : 0};
}

void RunSoak(const std::string& model, const std::string& snapshot, double minutes, uint32_t seed)
{
    SoakMachine m;
    ASSERT_TRUE(m.Create(model)) << model;
    if (!snapshot.empty())
        ASSERT_TRUE(m.emulator->LoadSnapshot((TestPathHelper::FindProjectRoot() / snapshot).string())) << snapshot;
    else
        StartPentagonProgram(m);

    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-soak-" + model);
    const std::string dumps = TestPathHelper::GetUniqueTestScratchPath("ttd-soak-dumps-" + model);
    fs::create_directories(dumps);
    m.ttd->SetShadowRecordingRoot(root);
    m.ttd->SetHistoryLimit(0, 64ull << 20);
    m.ttd->SetCheckSeeks(true);
    ASSERT_TRUE(m.ttd->StartRecording());

    std::mt19937 rng(seed);
    auto rnd = [&](uint64_t lo, uint64_t hi) {
        return hi <= lo ? lo : std::uniform_int_distribution<uint64_t>(lo, hi)(rng);
    };
    const uint32_t frameT = m.context->config.frame ? m.context->config.frame : 71680;

    SoakCounts c;
    std::map<PosKey, uint64_t> seen;   ///< state at a position on a branch's lineage, from the first visit
    std::map<PosKey, std::vector<uint64_t>> seenParts;   ///< ... in parts, for the report of a mismatch
    const std::vector<std::string> partNames = m.PartNames();
    auto differingParts = [&](const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
        std::string out;
        int n = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            if (a[i] != b[i] && n++ < 12)
                out += " [" + (i < partNames.size() ? partNames[i] : std::to_string(i)) + "]";
        if (a.size() != b.size())
            out += " [part count " + std::to_string(a.size()) + " / " + std::to_string(b.size()) + "]";
        return out;
    };
    std::vector<uint32_t> created;     ///< live branches made by the soak, oldest first
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> branchSpan;   ///< a branch's fork frame and end frame
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const auto budget = std::chrono::duration<double, std::ratio<60>>(minutes);

    auto seekAndCheck = [&](uint32_t branch, uint64_t frame, uint32_t t) {
        ttd::TTDSeekResult r;
        ++c.seeks;
        if (t)
            ++c.seeksInFrame;
        if (!m.ttd->SeekTo({frame, t}, &r))
        {
            ++c.seekFailures;
            if (c.seekFailures <= 3)
                c.Note("seek failed: branch " + std::to_string(branch) + " frame " + std::to_string(frame) + " t " +
                       std::to_string(t) + ": halt " + std::to_string(int(r.haltReason)) + " arrived " +
                       std::to_string(r.arrivedAt.frame) + "/" + std::to_string(r.arrivedAt.tInFrame) +
                       (r.haltReason == ttd::TTDSeekHaltReason::ExternalEvent ? " marker kind " + std::to_string(int(r.blockingMarker.kind)) : std::string()));
            return;
        }
        if (r.checked)
            ++c.seekChecks;
        if (r.checkDiverged)
        {
            ++c.seekCheckDivergences;
            c.Note("seek check diverged: branch " + std::to_string(branch) + " frame " + std::to_string(frame) + " t " +
                   std::to_string(t) + ": " + r.checkText);
        }
        const PosKey key{branch, frame, t};
        const uint64_t h = m.StateHash();
        const auto it = seen.find(key);
        if (it == seen.end())
        {
            seen[key] = h;
            seenParts[key] = m.StateParts();
        }
        else
        {
            ++c.revisits;
            if (it->second != h)
            {
                ++c.revisitMismatches;
                c.Note("revisit mismatch: branch " + std::to_string(branch) + " frame " + std::to_string(frame) +
                       " t " + std::to_string(t) + ":" + differingParts(seenParts[key], m.StateParts()));
            }
        }
    };

    while (std::chrono::steady_clock::now() - start < budget)
    {
        ++c.cycles;
        // The trunk records on
        const uint64_t run = rnd(100, 400);
        for (uint64_t f = 0; f < run; ++f)
        {
            const uint32_t before = m.context->GetFrameTStates();
            m.emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            const uint32_t after = m.context->GetFrameTStates();
            c.clockSlowed += after < before ? 1 : 0;
            c.clockRaised += after > before ? 1 : 0;
        }
        c.framesRun += run;
        const ttd::TTDTimePoint trunkEnd = m.ttd->CurrentPosition();

        // Random seeks on the trunk (every third inside a frame), some revisits of known positions
        auto [lo, hi] = SeekRange(m, ttd::kTTDTrunk);
        for (int i = 0; i < 8; ++i)
            seekAndCheck(ttd::kTTDTrunk, rnd(lo, hi), i % 3 == 2 ? static_cast<uint32_t>(rnd(1, frameT - 1)) : 0);
        for (int i = 0; i < 4 && !seen.empty(); ++i)
        {
            auto it = seen.begin();
            std::advance(it, static_cast<long>(rnd(0, seen.size() - 1)));
            const auto [branch, frame, t] = it->first;
            if (branch != ttd::kTTDTrunk)
                continue;
            seekAndCheck(branch, frame, t);
        }

        // Branch here: a fork somewhere in the trunk, a key held there, a few frames recorded
        const uint64_t forkFrame = rnd(lo, hi);
        if (m.ttd->SeekTo({forkFrame, 0}) && m.ttd->ResumeRecordingFrom({forkFrame, 0}))
        {
            const uint32_t branch = m.ttd->ActiveBranch();
            if (branch == ttd::kTTDTrunk)
            {
                ++c.resumeCutTrunk;
                c.Note("a resume at frame " + std::to_string(forkFrame) + " cut the trunk");
            }
            else
            {
                ++c.branchesCreated;
                created.push_back(branch);
                auto* keys = m.context->pDebugManager->GetKeyboardManager();
                keys->PressKey(ZXKEY_A);
                m.emulator->RunNFrames(static_cast<int>(rnd(1, 5)), true);
                keys->ReleaseKey(ZXKEY_A);
                const uint64_t more = rnd(20, 120);
                m.emulator->RunNFrames(static_cast<int>(more), true);
                c.framesRun += more;
                const uint64_t branchEnd = m.ttd->CurrentPosition().frame;
                m.ttd->StopRecording();
                branchSpan[branch] = {forkFrame, branchEnd};
                // The branch's own part: from its fork to its last whole frame
                for (int i = 0; i < 4; ++i)
                    seekAndCheck(branch, rnd(forkFrame, branchEnd - 1), i == 3 ? static_cast<uint32_t>(rnd(1, frameT - 1)) : 0);
            }
        }

        // Switch to an older branch now and then, seek there
        if (created.size() > 1 && rnd(0, 2) == 0)
        {
            const uint32_t other = created[rnd(0, created.size() - 2)];
            ++c.switches;
            if (m.ttd->SwitchBranch(other, std::nullopt, error))
            {
                const auto [from, end] = branchSpan[other];
                for (int i = 0; i < 3; ++i)
                    seekAndCheck(other, rnd(from, end - 1), 0);
            }
            else
            {
                ++c.switchFailures;
                c.Note("switch to " + std::to_string(other) + ": " + error);
            }
        }

        // Back to the trunk's end, and the trunk records on there (the resume of bug 2)
        ++c.switches;
        ++c.trunkEndResumes;
        if (!m.ttd->SwitchBranch(ttd::kTTDTrunk, trunkEnd, error) || !m.ttd->ResumeRecordingFrom(trunkEnd) ||
            m.ttd->ActiveBranch() != ttd::kTTDTrunk)
        {
            ++c.trunkEndResumeFailures;
            c.Note("resume at the trunk's end (frame " + std::to_string(trunkEnd.frame) + "): " + error);
            if (m.ttd->GetSessionInfo().state != ttd::TTDSessionState::Recording)
                ASSERT_TRUE(m.ttd->ResumeRecordingLive()) << "the soak cannot go on";
        }

        // Prune at six branches: the oldest goes with its subtree
        while (created.size() > 6)
        {
            std::vector<uint32_t> removed;
            const uint32_t oldest = created.front();
            created.erase(created.begin());
            if (m.ttd->DeleteBranch(oldest, true, removed, error))
            {
                ++c.prunes;
                for (uint32_t id : removed)
                {
                    created.erase(std::remove(created.begin(), created.end(), id), created.end());
                    for (auto it = seen.begin(); it != seen.end();)
                    {
                        if (std::get<0>(it->first) == id)
                        {
                            seenParts.erase(it->first);
                            it = seen.erase(it);
                        }
                        else
                            ++it;
                    }
                }
            }
        }

        // Every eight cycles: dump, load into a fresh instance, compare known positions
        if (c.cycles % 8 == 0)
        {
            const std::string path = (fs::path(dumps) / ("cycle-" + std::to_string(c.cycles) + ".ttd")).string();
            ++c.dumps;
            const ttd::TimeTravelController::DumpResult d = m.ttd->DumpSession(path);
            m.ttd->ResumeRecordingLive();
            if (!d.ok)
            {
                ++c.dumpFailures;
                c.Note("dump at cycle " + std::to_string(c.cycles) + ": " + d.error);
            }
            else
            {
                SoakMachine fresh;
                ++c.loads;
                std::string err;
                if (!fresh.Create(model) || !fresh.ttd->DeserializeSessionFile(path, err))
                {
                    ++c.loadFailures;
                    c.Note("load at cycle " + std::to_string(c.cycles) + ": " + err);
                }
                else
                {
                    int compared = 0;
                    for (auto it = seen.begin(); it != seen.end() && compared < 24; ++it)
                    {
                        if (rnd(0, 3) != 0)
                            continue;
                        const auto [branch, frame, t] = it->first;
                        // A local id holds inside one session; the file's branches are numbered afresh on load (a
                        // pruned branch leaves no gap), the GUID is the branch's identity
                        const ttd::TTDBranchInfo* original = m.ttd->Branches().Get(branch);
                        const ttd::TTDBranchInfo* loaded = original ? fresh.ttd->Branches().FindGuid(original->guid) : nullptr;
                        bool ok = loaded != nullptr &&
                                  fresh.ttd->SwitchBranch(loaded->id, ttd::TTDTimePoint{frame, t}, err);
                        ok = ok && fresh.ttd->SeekTo({frame, t});
                        if (original && !loaded)
                        {
                            ++c.loadMismatches;
                            c.Note("loaded session lacks branch " + std::to_string(branch) + " (by GUID)");
                        }
                        if (!ok)
                            continue;
                        ++compared;
                        ++c.loadCompares;
                        if (fresh.StateHash() != it->second)
                        {
                            ++c.loadMismatches;
                            const ttd::TTDTimePoint landed = fresh.ttd->CurrentPosition();
                            c.Note("loaded mismatch at cycle " + std::to_string(c.cycles) + ": branch " +
                                   std::to_string(branch) + " frame " + std::to_string(frame) + " t " +
                                   std::to_string(t) + " (viewed branch " + std::to_string(fresh.ttd->ViewedBranch()) +
                                   ", landed " + std::to_string(landed.frame) + "/" + std::to_string(landed.tInFrame) +
                                   "):" + differingParts(seenParts[it->first], fresh.StateParts()));
                        }
                    }
                }
            }
            std::error_code ec;
            fs::remove(path, ec);
        }

        if (c.cycles % 20 == 0)
        {
            const ttd::TTDSessionInfo info = m.ttd->GetSessionInfo();
            std::printf("[soak %s] cycle %llu frames %llu seeks %llu checks %llu div %llu revisits %llu/%llu "
                        "readback loads %llu hits %llu failures %llu evicted %llu\n",
                        model.c_str(), (unsigned long long)c.cycles, (unsigned long long)c.framesRun,
                        (unsigned long long)c.seeks, (unsigned long long)c.seekChecks,
                        (unsigned long long)c.seekCheckDivergences, (unsigned long long)c.revisitMismatches,
                        (unsigned long long)c.revisits, (unsigned long long)info.readBackLoads,
                        (unsigned long long)info.readBackHits, (unsigned long long)info.readBackFailures,
                        (unsigned long long)info.evictedToDiskSegments);
            std::fflush(stdout);
        }
    }
    m.ttd->StopRecording();

    const ttd::TTDSessionInfo info = m.ttd->GetSessionInfo();
    std::printf("[soak %s] DONE seed %u: cycles %llu, frames run %llu, session %llu-%llu\n"
                "  seeks %llu (in a frame %llu, failed %llu), seek checks %llu, divergences %llu\n"
                "  revisits %llu, mismatches %llu\n"
                "  branches %llu, switches %llu (failed %llu), trunk-end resumes %llu (failed %llu), resumes that cut "
                "the trunk %llu, prunes %llu\n"
                "  dumps %llu (failed %llu), loads %llu (failed %llu), loaded compares %llu, mismatches %llu\n"
                "  disk tier %d, evicted segments %llu, read back loads %llu, hits %llu, failures %llu, engine seek "
                "checks %llu / divergences %llu\n",
                model.c_str(), seed, (unsigned long long)c.cycles, (unsigned long long)c.framesRun,
                (unsigned long long)info.sessionStartFrame, (unsigned long long)info.currentEndFrame,
                (unsigned long long)c.seeks, (unsigned long long)c.seeksInFrame, (unsigned long long)c.seekFailures,
                (unsigned long long)c.seekChecks, (unsigned long long)c.seekCheckDivergences,
                (unsigned long long)c.revisits, (unsigned long long)c.revisitMismatches,
                (unsigned long long)c.branchesCreated, (unsigned long long)c.switches,
                (unsigned long long)c.switchFailures, (unsigned long long)c.trunkEndResumes,
                (unsigned long long)c.trunkEndResumeFailures, (unsigned long long)c.resumeCutTrunk,
                (unsigned long long)c.prunes, (unsigned long long)c.dumps, (unsigned long long)c.dumpFailures,
                (unsigned long long)c.loads, (unsigned long long)c.loadFailures, (unsigned long long)c.loadCompares,
                (unsigned long long)c.loadMismatches, info.diskTierActive ? 1 : 0,
                (unsigned long long)info.evictedToDiskSegments, (unsigned long long)info.readBackLoads,
                (unsigned long long)info.readBackHits, (unsigned long long)info.readBackFailures,
                (unsigned long long)info.seekChecks, (unsigned long long)info.seekCheckDivergences);
    std::printf("  CPU clock changes on the trunk: slower %llu, faster %llu\n", (unsigned long long)c.clockSlowed,
                (unsigned long long)c.clockRaised);
    for (const std::string& n : c.notes)
        std::printf("  NOTE %s\n", n.c_str());

    EXPECT_EQ(c.seekCheckDivergences, 0u);
    EXPECT_EQ(c.revisitMismatches, 0u);
    EXPECT_EQ(c.loadMismatches, 0u);
    EXPECT_EQ(info.readBackFailures, 0u);
    EXPECT_EQ(c.resumeCutTrunk, 0u);
    EXPECT_EQ(c.trunkEndResumeFailures, 0u);
    // A short soak may stay under the history limit; once segments went to disk the seeks must read some back
    if (info.evictedToDiskSegments > 0)
        EXPECT_GT(info.readBackLoads, 0u) << "segments went to disk and no seek read one back";

    std::error_code ec;
    fs::remove_all(dumps, ec);
}
}  // namespace

TEST(TTDSoak_Test, TslSlideshowWithTheDiskTier)
{
    const double minutes = SoakMinutes();
    if (minutes <= 0)
        GTEST_SKIP() << "a soak: set UNREAL_TTD_SOAK_MINUTES";
    RunSoak("TSL", "testdata/machines/tsconf/spg/slideshow.spg", minutes, SoakSeed());
}

TEST(TTDSoak_Test, PentagonControl)
{
    const double minutes = SoakMinutes();
    if (minutes <= 0)
        GTEST_SKIP() << "a soak: set UNREAL_TTD_SOAK_MINUTES";
    RunSoak("PENTAGON", "", minutes, SoakSeed());
}
