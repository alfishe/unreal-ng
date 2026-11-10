#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// F-2 (docs/inprogress/2026-11-08-ttd-branches/test-strategy.md §3, P1 part: memory only): seeded random programs of
/// branch operations - record, seek, resume in the past, an edit in the past, switch, delete - against a model of
/// the tree and of every recorded frame start's RAM. After every operation the tree equals the model (§4.1 inv. 2),
/// and every seek lands on the RAM the machine had when it recorded that frame (inv. 1).
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass, so a key held on one branch and not on
/// another makes their RAM differ from that frame on.
///
/// Runtime justification: a seed runs 60 operations of a few frames each on a small program (~0.3 s); the long tier
/// runs 20 seeds of 200 operations.
namespace
{
/// Deterministic on every platform (std distributions are not): a 64-bit LCG, the high bits used
struct Rng
{
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 33);
    }
    uint32_t Below(uint32_t n) { return n == 0 ? 0 : Next() % n; }
};

struct ModelBranch
{
    uint32_t parent = ttd::kTTDTrunk;
    uint64_t fork = 0;                    ///< the frame it forks at (its own frames start there)
    uint64_t end = 0;                     ///< its last recorded frame start
    /// Its own frames -> where the machine stopped in it (a run ends at the first instruction boundary of a frame,
    /// some T-states in) and the RAM there
    std::map<uint64_t, std::pair<uint32_t, uint64_t>> ram;
    bool live = true;
};
}  // namespace

class TimeTravelController_BranchesFuzz_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    std::map<uint32_t, ModelBranch> _model;
    uint32_t _active = ttd::kTTDTrunk;
    std::vector<std::string> _ops;   ///< the program so far, printed on a failure

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();

        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0xDB, 0xFE,        // 8004 loop: IN A,(#FE)
            0x86,              // 8006 ADD A,(HL)
            0x77,              // 8007 LD (HL),A
            0x23,              // 8008 INC HL
            0x7C,              // 8009 LD A,H  -- wrap #7FFF -> #6000
            0xE6, 0x1F,        // 800A AND #1F
            0xF6, 0x60,        // 800C OR #60
            0x67,              // 800E LD H,A
            0x18, 0xF3,        // 800F JR loop
        };
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint64_t Ram() const
    {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t a = 0x6000; a < 0x8000; ++a)
            h = (h ^ _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a))) * 1099511628211ull;
        return h;
    }

    uint64_t Frame() const { return _context->emulatorState.frame_counter; }

    std::string Program() const
    {
        std::ostringstream s;
        for (size_t i = 0; i < _ops.size(); ++i)
            s << "  " << i << ": " << _ops[i] << "\n";
        return s.str();
    }

    /// The branch of @p branch's lineage that holds frame @p frame
    uint32_t Owner(uint32_t branch, uint64_t frame) const
    {
        uint32_t id = branch;
        while (id != ttd::kTTDTrunk && frame < _model.at(id).fork)
            id = _model.at(id).parent;
        return id;
    }

    std::optional<uint64_t> RamOn(uint32_t branch, uint64_t frame) const
    {
        const ModelBranch& b = _model.at(Owner(branch, frame));
        const auto it = b.ram.find(frame);
        return it == b.ram.end() ? std::nullopt : std::optional<uint64_t>(it->second.second);
    }

    /// Where the model knows frame @p frame of @p branch's lineage (its frame start when unknown)
    ttd::TTDTimePoint At(uint32_t branch, uint64_t frame) const
    {
        const ModelBranch& b = _model.at(Owner(branch, frame));
        const auto it = b.ram.find(frame);
        return {frame, it == b.ram.end() ? 0u : it->second.first};
    }

    void Note(ModelBranch& b)
    {
        const ttd::TTDTimePoint here = _ttd->CurrentPosition();
        b.ram[here.frame] = {here.tInFrame, Ram()};
        b.end = here.frame;
    }

    /// The frames of @p branch's lineage the model knows the RAM of
    std::vector<uint64_t> KnownFrames(uint32_t branch) const
    {
        std::vector<uint64_t> frames;
        uint64_t upTo = UINT64_MAX;
        for (uint32_t id = branch;; id = _model.at(id).parent)
        {
            for (const auto& [f, h] : _model.at(id).ram)
                if (f < upTo && f <= _model.at(branch).end)
                    frames.push_back(f);
            if (id == ttd::kTTDTrunk)
                break;
            upTo = _model.at(id).fork;
        }
        return frames;
    }

    /// Run @p frames while recording on the active branch, the RAM of every frame start noted
    void Run(int frames, bool key)
    {
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        ModelBranch& b = _model.at(_active);
        for (int k = 0; k < frames; ++k)
        {
            if (key && k == 0)
                keys->PressKey(ZXKEY_A);
            _emulator->RunNFrames(1);
            if (key && k == 0)
                keys->ReleaseKey(ZXKEY_A);
            Note(b);
        }
    }

    /// A new branch of the active lineage at @p frame, active
    void ForkInModel(uint32_t id, uint64_t frame)
    {
        ModelBranch b;
        b.parent = Owner(_active, frame);
        b.fork = frame;
        b.end = frame;
        // It forks at the moment the parent's frame was noted at: the same RAM there, unless edited
        if (const std::optional<uint64_t> h = RamOn(_active, frame))
            b.ram[frame] = {At(_active, frame).tInFrame, *h};
        _model[id] = b;
        _active = id;
    }

    /// The tree equals the model (§4.1 invariant 2)
    void CheckTree()
    {
        size_t live = 0;
        for (const auto& [id, b] : _model)
        {
            if (!b.live)
            {
                EXPECT_EQ(_ttd->Branches().Get(id), nullptr) << "branch " << id << " deleted\n" << Program();
                continue;
            }
            ++live;
            const ttd::TTDBranchInfo* info = _ttd->Branches().Get(id);
            ASSERT_NE(info, nullptr) << "branch " << id << " missing\n" << Program();
            if (id != ttd::kTTDTrunk)
            {
                EXPECT_EQ(info->parent, b.parent) << "branch " << id << "\n" << Program();
                EXPECT_EQ(info->fork.frame, b.fork) << "branch " << id << "\n" << Program();
            }
        }
        EXPECT_EQ(_ttd->Branches().Count(), live) << Program();
        EXPECT_EQ(_ttd->ActiveBranch(), _active) << Program();
    }

    /// Seek on the active lineage, the RAM as recorded (§4.1 invariant 1)
    void SeekAndCheck(uint64_t frame)
    {
        ASSERT_TRUE(_ttd->SeekTo(At(_active, frame))) << "seek " << frame << "\n" << Program();
        if (const std::optional<uint64_t> h = RamOn(_active, frame))
            EXPECT_EQ(Ram(), *h) << "frame " << frame << " on branch " << _active << "\n" << Program();
    }

    void RunSeed(uint64_t seed, int operations)
    {
        Rng rng(seed);
        _model.clear();
        _ops.clear();
        _active = ttd::kTTDTrunk;
        ASSERT_TRUE(_ttd->StartRecording());
        _model[ttd::kTTDTrunk] = ModelBranch{};
        _model[ttd::kTTDTrunk].fork = Frame();
        Note(_model[ttd::kTTDTrunk]);
        Run(8, false);
        _ops.push_back("record 8");

        for (int op = 0; op < operations && !HasFatalFailure(); ++op)
        {
            const uint32_t kind = rng.Below(100);
            const std::vector<uint64_t> known = KnownFrames(_active);
            if (kind < 30)
            {
                // Run on: at the active end it continues, elsewhere it resumes in the past (a fork)
                const int frames = 1 + static_cast<int>(rng.Below(3));
                const bool key = rng.Below(2) == 0;
                if (_ttd->GetState() != ttd::TTDSessionState::Recording)
                {
                    const ttd::TTDTimePoint at = _ttd->CurrentPosition();
                    const uint64_t here = at.frame;
                    const bool atEnd = here == _model.at(_active).end;
                    if (atEnd)
                        ASSERT_TRUE(_ttd->ResumeRecordingLive()) << Program();
                    else
                    {
                        ASSERT_TRUE(_ttd->ResumeRecordingFrom(at)) << Program();   // the exact moment (Q-2)
                        ForkInModel(_ttd->ActiveBranch(), here);
                    }
                    _ops.push_back(std::string(atEnd ? "continue" : "resume") + " at " + std::to_string(here));
                }
                Run(frames, key);
                _ops.push_back("run " + std::to_string(frames) + (key ? " key" : ""));
            }
            else if (kind < 60)
            {
                const uint64_t frame = known[rng.Below(static_cast<uint32_t>(known.size()))];
                _ops.push_back("seek " + std::to_string(frame));
                SeekAndCheck(frame);
            }
            else if (kind < 70)
            {
                // An edit in the past forks; the edited frame's RAM is the edit's, the frames after it recorded
                const uint64_t frame = known[rng.Below(static_cast<uint32_t>(known.size()))];
                if (frame == _model.at(_active).end)
                    continue;
                _ops.push_back("edit at " + std::to_string(frame));
                SeekAndCheck(frame);
                const uint8_t value = static_cast<uint8_t>(rng.Next());
                const std::string refused = _emulator->EditMemoryFromTool("fuzz", [&]() {
                    _context->pMemory->DirectWriteToZ80Memory(0x6000 + static_cast<uint16_t>(rng.Below(0x2000)), value);
                });
                ASSERT_TRUE(refused.empty()) << refused << "\n" << Program();
                ForkInModel(_ttd->ActiveBranch(), frame);
                _model.at(_active).ram.erase(frame);
                Run(1 + static_cast<int>(rng.Below(2)), false);
            }
            else if (kind < 85)
            {
                // Switch to a live branch at a frame of its lineage
                std::vector<uint32_t> live;
                for (const auto& [id, b] : _model)
                    if (b.live)
                        live.push_back(id);
                const uint32_t target = live[rng.Below(static_cast<uint32_t>(live.size()))];
                const std::vector<uint64_t> frames = KnownFrames(target);
                const uint64_t frame = frames[rng.Below(static_cast<uint32_t>(frames.size()))];
                _ops.push_back("switch " + std::to_string(target) + " at " + std::to_string(frame));
                std::string error;
                ASSERT_TRUE(_ttd->SwitchBranch(target, At(target, frame), error)) << error << "\n" << Program();
                _active = target;
                if (const std::optional<uint64_t> h = RamOn(target, frame))
                    EXPECT_EQ(Ram(), *h) << "switch to " << target << " at " << frame << "\n" << Program();
            }
            else
            {
                // Delete a live branch off the active lineage, with its subtree
                std::vector<uint32_t> lineage;
                for (uint32_t id = _active;; id = _model.at(id).parent)
                {
                    lineage.push_back(id);
                    if (id == ttd::kTTDTrunk)
                        break;
                }
                std::vector<uint32_t> off;
                for (const auto& [id, b] : _model)
                    if (b.live && std::find(lineage.begin(), lineage.end(), id) == lineage.end())
                        off.push_back(id);
                if (off.empty())
                    continue;
                const uint32_t victim = off[rng.Below(static_cast<uint32_t>(off.size()))];
                _ops.push_back("delete " + std::to_string(victim));
                std::vector<uint32_t> removed;
                std::string error;
                ASSERT_TRUE(_ttd->DeleteBranch(victim, /*force=*/true, removed, error)) << error << "\n" << Program();
                size_t gone = 0;
                for (bool changed = true; changed;)
                {
                    changed = false;
                    for (auto& [id, b] : _model)
                        if (b.live && (id == victim || (id != ttd::kTTDTrunk && !_model.at(b.parent).live)))
                        {
                            b.live = false;
                            changed = true;
                            ++gone;
                        }
                }
                EXPECT_EQ(removed.size(), gone) << "the subtree goes with it\n" << Program();
            }
            CheckTree();
        }
        // Every known frame of every live branch restores to its RAM (§4.1 invariant 1, the whole tree)
        for (const auto& [id, b] : _model)
        {
            if (!b.live || HasFatalFailure())
                continue;
            for (const uint64_t frame : KnownFrames(id))
            {
                std::string error;
                ASSERT_TRUE(_ttd->SwitchBranch(id, At(id, frame), error)) << error << "\n" << Program();
                _active = id;
                if (const std::optional<uint64_t> h = RamOn(id, frame))
                    EXPECT_EQ(Ram(), *h) << "final: frame " << frame << " on branch " << id << "\n" << Program();
            }
        }
        _ttd->StopRecording();
    }
};

TEST_F(TimeTravelController_BranchesFuzz_Test, RandomProgramsKeepTheTreeAndEveryFrame)
{
    const int seeds = TestTier::Pick(1, 20);
    const int operations = TestTier::Pick(60, 200);
    for (int seed = 1; seed <= seeds && !HasFailure(); ++seed)
    {
        SCOPED_TRACE("seed " + std::to_string(seed));
        if (seed > 1)
        {
            TearDown();
            SetUp();
        }
        RunSeed(static_cast<uint64_t>(seed), operations);
    }
}

