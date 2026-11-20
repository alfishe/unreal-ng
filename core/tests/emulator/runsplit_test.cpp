// Split-independent direct runs (docs/inprogress/2026-11-12-re-api-efficiency, F2): a run of N frames ends at the
// same T-state with the same machine state however it is split into calls - one call, two calls, N calls of one
// frame, or slices of run_tstates on the timeline. Before the fix every call dropped its last instruction's overshoot
// and counted the next call from where the machine stood, so a run in calls drifted by a few T-states per call.

#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/snapshotdigest.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testtier.h"
#include "debugger/targets/cpuview.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

namespace
{

/// FNV-1a 64
uint64_t Fnv(const uint8_t* data, size_t size, uint64_t h = 14695981038346656037ull)
{
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

/// Everything a run leaves behind: the position, the snapshot digest (RAM, latches, CPU, AY, border), every device's
/// whole state as time travel saves it (sound cards with their CPU and RAM, the tape, the disk interface) and the
/// picture
struct MachineDigest
{
    uint64_t frame = 0;
    uint32_t t = 0;
    SnapshotDigest::Digest snapshot;
    uint64_t devices = 0;
    size_t deviceCount = 0;
    uint64_t picture = 0;

    bool operator==(const MachineDigest& o) const
    {
        return frame == o.frame && t == o.t && snapshot.ram == o.snapshot.ram && snapshot.ports == o.snapshot.ports &&
               snapshot.cpu == o.snapshot.cpu && snapshot.ay == o.snapshot.ay && snapshot.misc == o.snapshot.misc &&
               devices == o.devices && deviceCount == o.deviceCount && picture == o.picture;
    }

    std::string Text() const
    {
        char devicesText[64];
        std::snprintf(devicesText, sizeof(devicesText), " devices=%08x(%zu) picture=%08x",
                      static_cast<uint32_t>(devices), deviceCount, static_cast<uint32_t>(picture));
        return "frame=" + std::to_string(frame) + " t=" + std::to_string(t) + " " + SnapshotDigest::ToText(snapshot) +
               devicesText;
    }
};

MachineDigest Capture(Emulator* emulator)
{
    MachineDigest d;
    EmulatorContext* context = emulator->GetContext();
    d.frame = context->emulatorState.frame_counter;
    d.t = context->pCore->GetZ80()->t;
    d.snapshot = SnapshotDigest::Capture(emulator);

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    EXPECT_TRUE(ttd::RegisterMachinePeripherals(context, registry, owned, &error)) << error;
    std::vector<std::vector<uint8_t>> states;
    registry.SaveLive(states);
    d.deviceCount = states.size();
    uint64_t h = Fnv(nullptr, 0);
    for (const std::vector<uint8_t>& state : states)
    {
        const uint64_t size = state.size();
        h = Fnv(reinterpret_cast<const uint8_t*>(&size), sizeof(size), h);
        h = Fnv(state.data(), state.size(), h);
    }
    d.devices = h;

    uint32_t* buffer = nullptr;
    size_t size = 0;
    context->pScreen->GetFramebufferData(&buffer, &size);
    d.picture = buffer != nullptr ? Fnv(reinterpret_cast<const uint8_t*>(buffer), size) : 0;
    return d;
}

/// A machine set up the same way each time: power-on RAM zero, the card the variant asks for, a common prefix that
/// leaves the CPU mid-frame (a run on the timeline must not depend on starting at a frame boundary)
struct Machine
{
    Emulator* emulator = nullptr;
    uint32_t frameLimit = 0;

    Machine(const std::string& model, GSTypeKind gs, bool selectCard)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        if (!emulator)
            return;
        EmulatorContext* context = emulator->GetContext();
        if (gs != GSTypeKind::NONE)
            EXPECT_TRUE(FitGeneralSoundCard(context->pSoundManager, gs)) << model;
        if (selectCard)
            EXPECT_EQ(CpuView::SetSelected(context, "gs"), "") << "a card a front-end shows follows in tight mode";
        emulator->RunTStates(12345);  // off the timeline: the ROM's boot runs for a while
        // A loop whose period (47 T) divides no frame length, so every run ends a few T past its target and a split
        // that dropped those T-states would drift; it writes the screen, the border and the General Sound's data
        // port (the card catches up at each host access): DI; LD HL,#4000; loop: LD (HL),A; INC L; INC A;
        // OUT (#FE),A; OUT (#B3),A; JP loop
        const uint8_t program[] = {0xF3, 0x21, 0x00, 0x40, 0x77, 0x2C, 0x3C, 0xD3, 0xFE, 0xD3, 0xB3, 0xC3, 0x04, 0x80};
        for (size_t i = 0; i < sizeof(program); ++i)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        Z80* z80 = context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->halted = 0;
        emulator->RunTStates(1234);  // off the timeline: the start of every variant, mid-frame, inside the loop
        frameLimit = z80->_frameLimit;
    }
    ~Machine()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
};

struct Variant
{
    const char* name;
    std::function<void(Emulator&, unsigned frames, uint32_t frameLimit)> run;
};

const std::vector<Variant>& Variants()
{
    static const std::vector<Variant> variants = {
        {"one call", [](Emulator& e, unsigned n, uint32_t) { e.RunNFrames(n); }},
        {"two calls", [](Emulator& e, unsigned n, uint32_t) {
             e.RunNFrames(n * 3 / 10);
             e.RunNFrames(n - n * 3 / 10);
         }},
        {"one frame per call", [](Emulator& e, unsigned n, uint32_t) {
             for (unsigned i = 0; i < n; ++i)
                 e.RunNFrames(1);
         }},
        {"frames then t-states on the timeline", [](Emulator& e, unsigned n, uint32_t limit) {
             e.RunNFrames(n / 2);
             e.RunTStates(static_cast<uint64_t>(n - n / 2) * limit, true, true);
         }},
        {"odd t-state slices on the timeline", [](Emulator& e, unsigned n, uint32_t limit) {
             const uint64_t total = static_cast<uint64_t>(n) * limit;
             uint64_t ran = 0;
             for (uint64_t slice = 7; ran < total; slice = slice * 5 + 3)
             {
                 const uint64_t step = std::min<uint64_t>(slice % 50000 + 1, total - ran);
                 e.RunTStates(step, true, true);
                 ran += step;
             }
         }},
    };
    return variants;
}

/// Every variant on a fresh machine ends where the one-call run ends, with the same state
void ExpectSplitIndependent(const std::string& model, GSTypeKind gs, unsigned frames, bool selectCard = false)
{
    // The sound devices as configured (the runner leaves them out by default): the AY / TurboSound cores catch up
    // when a direct run ends, the General Sound at host port accesses and frame ends
    SoundCardScope sound(TestSound::GeneralSound | TestSound::TurboSound);
    std::vector<MachineDigest> digests;
    for (const Variant& variant : Variants())
    {
        Machine m(model, gs, selectCard);
        ASSERT_NE(m.emulator, nullptr) << model;
        variant.run(*m.emulator, frames, m.frameLimit);
        digests.push_back(Capture(m.emulator));
        if (digests.size() > 1)
            EXPECT_TRUE(digests.back() == digests.front())
                << model << ", " << variant.name << " vs " << Variants().front().name << ":\n  "
                << digests.back().Text() << "\n  " << digests.front().Text();
    }
    EXPECT_GE(digests.front().frame, frames) << model;
}

}  // namespace

/// Pentagon with its shipped TurboSound and General Sound card: 20 frames five ways
TEST(RunSplit_Test, PentagonRunsAreSplitIndependent)
{
    ExpectSplitIndependent("PENTAGON", GSTypeKind::NONE, TestTier::Long() ? 400 : 20);
}

/// The 48K: contended memory, its own frame length
TEST(RunSplit_Test, Spectrum48KRunsAreSplitIndependent)
{
    ExpectSplitIndependent("48K", GSTypeKind::NONE, TestTier::Long() ? 400 : 20);
}

/// A classic General Sound (the shipped card is NeoGS): its Z80 catches up with the main CPU at host port accesses
/// and frame ends
TEST(RunSplit_Test, GeneralSoundRunsAreSplitIndependent)
{
    ExpectSplitIndependent("PENTAGON", GSTypeKind::Z80, TestTier::Long() ? 400 : 20);
}

/// The General Sound shown by a debugger front-end follows each direct run in tight mode
TEST(RunSplit_Test, TightGeneralSoundRunsAreSplitIndependent)
{
    ExpectSplitIndependent("PENTAGON", GSTypeKind::Z80, TestTier::Long() ? 200 : 10, true);
}

/// The answer of a run: T-states and frame ends of this call; consecutive calls add up to one call's
TEST(RunSplit_Test, RunResultCountsWhatRan)
{
    Machine one("PENTAGON", GSTypeKind::NONE, false);
    Machine two("PENTAGON", GSTypeKind::NONE, false);
    ASSERT_NE(one.emulator, nullptr);
    ASSERT_NE(two.emulator, nullptr);
    const Emulator::DirectRunResult whole = one.emulator->RunNFrames(6);
    const Emulator::DirectRunResult first = two.emulator->RunNFrames(2);
    const Emulator::DirectRunResult second = two.emulator->RunNFrames(4);
    EXPECT_TRUE(whole.reached);
    EXPECT_TRUE(first.reached);
    EXPECT_TRUE(second.reached);
    EXPECT_EQ(whole.frames, 6u);
    EXPECT_EQ(first.frames + second.frames, 6u);
    EXPECT_EQ(first.tstates + second.tstates, whole.tstates);
    EXPECT_GE(whole.tstates, 6ull * one.frameLimit);
    EXPECT_LT(whole.tstates, 6ull * one.frameLimit + 64);  // one instruction past the target at most
}

/// A run_tstates already past its target on the timeline runs nothing; off the timeline (time travel's own runs)
/// it always runs one instruction, as before
TEST(RunSplit_Test, TStatesOnTheTimelineStartFromTheTarget)
{
    Machine m("PENTAGON", GSTypeKind::NONE, false);
    ASSERT_NE(m.emulator, nullptr);
    Z80* z80 = m.emulator->GetContext()->pCore->GetZ80();
    // One t-state on the timeline: one instruction, a few T past the target
    const Emulator::DirectRunResult first = m.emulator->RunTStates(1, true, true);
    ASSERT_TRUE(first.reached);
    ASSERT_GE(first.tstates, 4u);
    const uint32_t t = z80->t;
    // The next t-state is already spent: nothing runs
    const Emulator::DirectRunResult second = m.emulator->RunTStates(1, true, true);
    EXPECT_TRUE(second.reached);
    EXPECT_EQ(second.tstates, 0u);
    EXPECT_EQ(z80->t, t);
    // Off the timeline the old rule: at least one instruction
    const Emulator::DirectRunResult off = m.emulator->RunTStates(1, true, false);
    EXPECT_GE(off.tstates, 4u);
    EXPECT_GT(z80->t, t);
}

/// Another run in between (here: a step) ends the timeline: the next run counts a whole frame from where it stands
TEST(RunSplit_Test, AnotherRunEndsTheTimeline)
{
    Machine m("PENTAGON", GSTypeKind::NONE, false);
    ASSERT_NE(m.emulator, nullptr);
    m.emulator->RunNFrames(1);
    m.emulator->RunSingleCPUCycle(true);
    const Emulator::DirectRunResult r = m.emulator->RunNFrames(1);
    EXPECT_TRUE(r.reached);
    EXPECT_GE(r.tstates, m.frameLimit);
}

/// The control: the same split off the timeline (the rule before the fix: each call counts from where the machine
/// stands) drifts by the overshoots, so the variants above would tell a regression apart
TEST(RunSplit_Test, OffTheTimelineASplitRunDrifts)
{
    Machine one("PENTAGON", GSTypeKind::NONE, false);
    Machine split("PENTAGON", GSTypeKind::NONE, false);
    ASSERT_NE(one.emulator, nullptr);
    ASSERT_NE(split.emulator, nullptr);
    one.emulator->RunNFrames(10);
    for (int i = 0; i < 10; ++i)
        split.emulator->RunTStates(split.frameLimit, true, /*onTimeline=*/false);
    const uint64_t at = one.emulator->GetContext()->emulatorState.frame_counter * one.frameLimit +
                        one.emulator->GetContext()->pCore->GetZ80()->t;
    const uint64_t drifted = split.emulator->GetContext()->emulatorState.frame_counter * split.frameLimit +
                             split.emulator->GetContext()->pCore->GetZ80()->t;
    EXPECT_GT(drifted, at);
}
