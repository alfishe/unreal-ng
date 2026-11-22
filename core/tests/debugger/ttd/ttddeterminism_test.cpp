/// @file ttddeterminism_test.cpp
/// @brief The same machine run twice with the same inputs gives the same state in every frame: CPU, all RAM and every
/// device blob the TTD records. And where a machine reads the host (the CMOS clock on the host's time, power-on RAM
/// noise), the recording holds what it read, so its replay is exact.
///
/// Background (2026-11-22): the TD-5 timeline measurement (2026-11-21) saw the ATM3's `betadisk` and
/// `evoavrvolatile` blobs change in a different number of idle frames from run to run (57-58, 30-78). Run here
/// frame by frame, the machine is deterministic: the differences came from two inputs that differ by design between
/// application runs - the CMOS clock (the host's live time until a recording anchors it at the session's wall time;
/// the ATM3 ROM's menu reads the Gluk clock every frame, so the clock's sub-second phase moves the AVR's /WAIT timing,
/// `evoavrvolatile`, and the code paths) and the power-on RAM noise (RAMPowerOn=Random). With the same anchor and
/// zero RAM two runs match in every byte; the anchor alone moved the timeline count of `evoavrvolatile` over 100
/// idle frames between 31 and 61.
///
/// The run found one real fault, in the replay: a frame in which the guest slows its CPU clock (the ATM3 boot's frame
/// 20, 14 -> 3.5 MHz) replayed three frames past its end (AFrameThatSlowsTheClockReplaysToItsEnd).
///
/// Runtime: each case boots a model for a few frames (ATM3: 20-25 frames, 0.2-0.6 s); the sweep over every creatable
/// model records 3 frames twice each, a sample of five in the default run (every model and 100 frames in the long
/// tier).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdreplaycheck.h"
#include "debugger/ttd/ttdtimeline.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace
{
uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

/// The machine after a frame
struct Frame
{
    ttd::TTDTimePoint at;   ///< where the frame ended (the run's position after it)
    uint64_t ram = 0;
    std::vector<uint8_t> cpu;
    std::map<std::string, std::vector<uint8_t>> blobs;
};

enum class Clock
{
    TestAnchor,   ///< the tests' emulated time from 2026-01-01 12:00:00 (EmulatorTestHelper), plus an offset
    Live,         ///< the application's: the host's live time until the recording anchors it at the session's wall
};

struct RunOptions
{
    Clock clock = Clock::TestAnchor;
    int64_t anchorOffsetMicros = 0;
    int bootFrames = 0;   ///< run before the recording starts
    int frames = 25;      ///< recorded
};

class Machine
{
public:
    explicit Machine(const std::string& model, RamPowerOn ram = RamPowerOn::Zero)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, ram);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
    }
    ~Machine()
    {
        if (_emulator)
        {
            _ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }
    bool Valid() const { return _emulator && _ttd; }
    ttd::TimeTravelController& Ttd() { return *_ttd; }

    uint32_t FrameTStates() const { return _context->GetFrameTStates(); }

    Ds12887* Rtc() const { return _context->pPortDecoder ? _context->pPortDecoder->GetRtcBinding().chip : nullptr; }

    /// Records options.frames frames, the machine after each
    std::vector<Frame> Record(const RunOptions& options)
    {
        if (Ds12887* rtc = Rtc())
        {
            if (options.clock == Clock::Live)
                rtc->UseLiveTime();
            else
                rtc->AnchorEmulatedTime(Ds12887::ToMicros({2026, 1, 1, 12, 0, 0}) + options.anchorOffsetMicros);
        }
        if (options.bootFrames)
            _emulator->RunNFrames(options.bootFrames, /*skipBreakpoints=*/true);
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        std::vector<Frame> out;
        if (!_ttd->StartRecording())
            return out;
        for (int f = 0; f < options.frames; ++f)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            out.push_back(Capture());
            out.back().at = _ttd->CurrentPosition();
        }
        _ttd->StopRecording();
        return out;
    }

    Frame Capture() const
    {
        Frame fr;
        fr.ram = Fnv1a(_context->pMemory->RAMPageAddress(0), size_t(_ttd->GetModelRamPages()) * 0x4000);
        const Z80* z80 = _context->pCore->GetZ80();
        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*z80);
        fr.cpu.assign(reinterpret_cast<const uint8_t*>(&cpu), reinterpret_cast<const uint8_t*>(&cpu) + sizeof(cpu));
        for (const ttd::TTDRegistryEntry& e : _ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0)
                e.device->TTDSaveStateTo(fr.blobs[e.key.instance]);
        return fr;
    }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
};

/// The first frame and part where two runs differ, empty when they match in every frame
std::string FirstDivergence(const std::vector<Frame>& a, const std::vector<Frame>& b)
{
    if (a.size() != b.size())
        return "frames " + std::to_string(a.size()) + " / " + std::to_string(b.size());
    for (size_t f = 0; f < a.size(); ++f)
    {
        std::string what;
        for (const auto& [name, blob] : a[f].blobs)
        {
            const auto it = b[f].blobs.find(name);
            if (it == b[f].blobs.end() || it->second != blob)
            {
                size_t at = 0;
                while (it != b[f].blobs.end() && at < blob.size() && at < it->second.size() &&
                       blob[at] == it->second[at])
                    ++at;
                what += " " + name + " at byte " + std::to_string(at);
            }
        }
        if (a[f].blobs.size() != b[f].blobs.size())
            what += " device set";
        if (a[f].ram != b[f].ram)
            what += " RAM";
        if (a[f].cpu != b[f].cpu)
            what += " CPU";
        if (!what.empty())
            return "frame " + std::to_string(f) + ":" + what;
    }
    return {};
}

std::vector<Frame> RecordOnce(const std::string& model, const RunOptions& options)
{
    Machine m(model);
    if (!m.Valid())
        return {};
    return m.Record(options);
}
}  // namespace

/// The ATM3 from its ROM, twice, the same clock anchor and zero RAM: every device blob, RAM and the CPU match in
/// every frame (its boot reads the Gluk clock through the AVR: ds12887, evoavrvolatile)
TEST(TTDDeterminism_Test, Atm3IsTheSameTwice)
{
    RunOptions o;
    o.frames = TestTier::Pick(25, 600);
    const std::vector<Frame> a = RecordOnce("ATM3", o);
    const std::vector<Frame> b = RecordOnce("ATM3", o);
    ASSERT_EQ(a.size(), static_cast<size_t>(o.frames));
    ASSERT_TRUE(a.front().blobs.count("evoavrvolatile") && a.front().blobs.count("ds12887") &&
                a.front().blobs.count("betadisk"))
        << "the blobs of the TD-5 measurement";
    EXPECT_EQ(FirstDivergence(a, b), "");
}

/// On the application's clock (the host's live time, then the session's wall time) two ATM3 runs differ - by design,
/// the clock is the host's - and the recording holds the time it ran on: the replay of every frame reproduces the
/// recorded machine, and a seek to every frame gives the state the live run had there
TEST(TTDDeterminism_Test, Atm3OnTheHostClockReplaysExactly)
{
    Machine m("ATM3");
    ASSERT_TRUE(m.Valid());
    RunOptions o;
    o.clock = Clock::Live;
    o.bootFrames = 5;
    o.frames = TestTier::Pick(20, 300);
    const std::vector<Frame> live = m.Record(o);
    ASSERT_EQ(live.size(), static_cast<size_t>(o.frames));

    ttd::TTDReplayCheckQuery q;
    q.limit = 1;
    const ttd::TTDReplayCheckResult check = m.Ttd().QueryReplayCheck(q);
    ASSERT_TRUE(check.error.empty()) << check.error;
    EXPECT_GE(check.checked, static_cast<uint64_t>(o.frames - 2));
    EXPECT_EQ(check.diverged, 0u) << ttd::TTDReplayCheckText(check);

    // Seeks, newest first: the clock's anchor and everything it drove come back with the frame
    for (size_t i = live.size(); i-- > 0;)
    {
        ASSERT_TRUE(m.Ttd().SeekTo(live[i].at)) << "frame " << live[i].at.frame;
        Frame seen = m.Capture();
        seen.at = live[i].at;
        EXPECT_EQ(FirstDivergence({live[i]}, {seen}), "") << "recorded frame " << i;
        if (HasFailure())
            return;
    }
}

/// A frame in which the guest slows its CPU clock replays to its own end. The ATM3 ROM leaves 14 MHz for 3.5 MHz in
/// its boot's frame 20: the frame's remainder, counted at 14 MHz before the replay ran it, is four 3.5 MHz frames, and
/// the replay of that frame ran three frames past its end (replay check f20 -> f21: frame_counter #18 for #15, PC,
/// SP, RAM and the Covox differing; every seek check of that frame diverged). The replay now stops at the frame's end
TEST(TTDDeterminism_Test, AFrameThatSlowsTheClockReplaysToItsEnd)
{
    Machine m("ATM3");
    ASSERT_TRUE(m.Valid());
    RunOptions o;
    o.frames = 23;
    const std::vector<Frame> live = m.Record(o);
    ASSERT_EQ(live.size(), static_cast<size_t>(o.frames));

    // The frame that slows the clock: x4 at its start, x1 at the next one (live run frames are 0-based from frame 0)
    ttd::TTDTimePoint at{};
    uint64_t slowed = UINT64_MAX;
    uint32_t before = 0;
    for (uint64_t f = 0; f + 1 < live.size() && slowed == UINT64_MAX; ++f)
    {
        ASSERT_TRUE(m.Ttd().CheckpointMoment(f, at));
        ASSERT_TRUE(m.Ttd().SeekTo(at));
        const uint32_t here = m.FrameTStates();
        if (before && here < before)
            slowed = f - 1;
        before = here;
    }
    ASSERT_NE(slowed, UINT64_MAX) << "the ATM3 boot no longer leaves its turbo clock";

    ttd::TTDReplayCheckQuery q;
    q.hasFrom = q.hasTo = true;
    q.fromFrame = q.toFrame = slowed;
    const ttd::TTDReplayCheckResult check = m.Ttd().QueryReplayCheck(q);
    ASSERT_EQ(check.checked, 1u) << ttd::TTDReplayCheckText(check);
    EXPECT_EQ(check.diverged, 0u) << ttd::TTDReplayCheckText(check);

    // The seek check of a seek into that frame
    m.Ttd().SetCheckSeeks(true);
    ttd::TTDSeekResult r;
    ASSERT_TRUE(m.Ttd().SeekTo({slowed, 1000}, &r));
    EXPECT_TRUE(r.checked);
    EXPECT_FALSE(r.checkDiverged) << r.checkText;
}

/// The sweep: every creatable model, twice from its ROM with the same clock and zero RAM, matches in every frame
TEST(TTDDeterminism_Test, EveryModelIsTheSameTwice)
{
    RunOptions o;
    o.frames = TestTier::Pick(3, 100);
    size_t checked = 0;
    // A sample in the default run (the clones with their own clocks, chips and boot ROMs; the ATM3 has its own cases
    // above), every model in the long tier
    const std::vector<std::string> sample = {"TSL", "SPRINTER", "GMX", "PROFI", "NEXT"};
    for (const TMemModel& model : Config::GetAvailableModels())
    {
        if (!Config::IsModelCreatable(model))
            continue;
        if (!TestTier::Long() && std::find(sample.begin(), sample.end(), model.ShortName) == sample.end())
            continue;
        SCOPED_TRACE(model.ShortName);
        const std::vector<Frame> a = RecordOnce(model.ShortName, o);
        const std::vector<Frame> b = RecordOnce(model.ShortName, o);
        ASSERT_EQ(a.size(), static_cast<size_t>(o.frames)) << model.ShortName << " did not record";
        ++checked;
        EXPECT_EQ(FirstDivergence(a, b), "") << model.ShortName;
    }
    EXPECT_GE(checked, TestTier::Long() ? 15u : sample.size()) << "the creatable models went missing";
}
