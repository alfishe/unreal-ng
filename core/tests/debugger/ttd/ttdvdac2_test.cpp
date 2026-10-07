/// @file ttdvdac2_test.cpp
/// @brief TTD with the TS-Conf VDAC2 card's FT812 (line-budget-metrics.md §3.4):
/// nothing of the chip is lost across a checkpoint, and a TTD position shows
/// what the live machine showed there.
///
/// - Seek by frame number: the FT812 picture the monitor showed at the end of
///   that frame (the FT812 frame that finished last), and the card's and chip's
///   state and memory exactly as the live run had them at that position.
/// - Seek to a T-state inside a frame: the FT812 frame drawn up to that moment
///   over its previous frame.
/// - A session written to a stream and read back seeks the same way.
///
/// Workload: test6.spg of the TS-Labs FT812 SDK - a 1940 x 768 "DXT" image
/// uploaded to RAM_G by DMA, scrolled every frame, so every frame differs and
/// RAM_G is full. Runtime: booting the program to its scrolling picture and
/// drawing 1024 x 768 FT812 frames takes about a second (justified: a real
/// program on the real machine is the point).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/platforms/tsconf/vdac2card.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/loaderspg.h"

#if ENABLE_VDAC2
#include <eve/eve.h>
#endif

#if ENABLE_VDAC2

namespace
{
struct Moment
{
    uint32_t t = 0;                 // CPU T-state in the frame (the exact position)
    std::vector<uint32_t> picture;  // what the monitor shows (the FT812 picture)
    uint64_t cardState = 0;         // card time, INT edges, chip control state (metrics block included)
    uint64_t chipMemory = 0;        // every FT812 memory region
    uint64_t metricsFrame = 0;      // the metrics block's frame number
};
}  // namespace

class TTDVdac2_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    Vdac2Card* _card = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-vdac2", "TSL-VDAC2", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        ASSERT_NE(decoder, nullptr);
        _card = decoder->GetVdac2Card();
        ASSERT_NE(_card, nullptr);
        ASSERT_TRUE(_card->IsReady());

        FeatureManager* fm = _emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);

        const std::string program =
            (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "vdac2-sdk" / "test6.spg").string();
        LoaderSPG loader(_context, program);
        ASSERT_TRUE(loader.load()) << program;

        // Until the program scrolls its picture: three different pictures in a row (about
        // 60 frames: the image goes to RAM_G by DMA first)
        std::vector<uint32_t> previous;
        int changes = 0;
        for (int i = 0; i < 400 && changes < 3; ++i)
        {
            _emulator->RunNFrames(1, true);
            std::vector<uint32_t> picture = Shown();
            changes = (_card->IsShowing() && !previous.empty() && picture != previous) ? changes + 1 : 0;
            previous = std::move(picture);
        }
        ASSERT_TRUE(_card->IsShowing()) << "test6 did not switch the monitor to the FT812";
        ASSERT_EQ(changes, 3) << "test6 did not start scrolling";
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
    }

    std::vector<uint32_t> Shown()
    {
        uint32_t* fb = nullptr;
        size_t size = 0;
        _context->pScreen->GetFramebufferData(&fb, &size);
        return std::vector<uint32_t>(fb, fb + size / sizeof(uint32_t));
    }

    /// The card and the TS-Conf engine (its DMA reaches the card) brought up to the
    /// CPU's position: both run lazily, so the same moment can sit at different
    /// catch-up points in a live run and after a restore
    void CatchUp()
    {
        dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder)->CatchUpEngine();
        _card->Synchronize();
    }

    Moment Now()
    {
        Moment m;
        m.t = static_cast<uint32_t>(_context->pCore->GetZ80()->t);
        m.picture = Shown();
        CatchUp();
        m.cardState = _card->TtdStateHash();
        m.chipMemory = _card->TtdMemoryHash();
        EveFrameMetrics metrics{};
        EveGetFrameMetrics(_card->Chip(), &metrics, nullptr, 0);
        m.metricsFrame = metrics.frame;
        return m;
    }

    /// Record `frames` frames. Returns, per frame number f, the live state where
    /// the run stopped after frame f-1 (position {f, t}) and the picture at the end of f
    void Record(unsigned frames, std::map<uint64_t, Moment>& stateAt, std::map<uint64_t, std::vector<uint32_t>>& finalPicture)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        stateAt[_context->emulatorState.frame_counter] = Now();
        for (unsigned i = 0; i < frames; ++i)
        {
            _emulator->RunNFrames(1, true);
            const uint64_t f = _context->emulatorState.frame_counter;
            finalPicture[f - 1] = Shown();
            stateAt[f] = Now();
        }
        _ttd->StopRecording();
    }
};

/// The FT812 is in the checkpoints: its blobs are registered and sized
TEST_F(TTDVdac2_Test, ChipBlobsAreRecorded)
{
    const auto ids = _context->pPortDecoder->GetTTDModelStateIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::PeripheralId::Vdac2Memory), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::PeripheralId::Vdac2), ids.end());
    EXPECT_GT(_card->TtdMemorySize(), 1024u * 1024u) << "RAM_G and the other regions, whole";
    EXPECT_GT(_card->TtdStateSize(), 0u);
}

/// Seek by frame number: picture of the frame's end, and state, memory and the
/// metrics block exactly as live at {f, 0}
TEST_F(TTDVdac2_Test, SeekByFrameMatchesTheLiveRun)
{
    std::map<uint64_t, Moment> stateAt;
    std::map<uint64_t, std::vector<uint32_t>> finalPicture;
    Record(10, stateAt, finalPicture);

    const uint64_t first = finalPicture.begin()->first + 1;  // the first frame has a full lead-in in the session
    const uint64_t last = finalPicture.rbegin()->first;
    std::set<std::vector<uint32_t>> distinct;
    for (const auto& [frame, picture] : finalPicture)
        distinct.insert(picture);
    ASSERT_GE(distinct.size(), finalPicture.size() / 2) << "the pictures must differ, or a seek proves nothing";
    int inexact = 0;
    // Back and forth, so every seek restores against a different live state
    for (uint64_t f : {last, first, first + 4, first + 1, last - 1, first + 2})
    {
        SCOPED_TRACE(f);
        // By frame number: the picture at the end of the frame
        ASSERT_TRUE(_ttd->SeekTo(_ttd->FrameEndPosition(f)));   // by frame number: its end (D13)
        EXPECT_TRUE(Shown() == finalPicture.at(f)) << "the picture at the end of frame " << f;

        // At the live run's exact position: every byte of the card, the chip and its memory.
        // TTD lands on the first instruction boundary at or after the T-state; at a few
        // frame starts that is a later one than the live run stopped at (a positioning
        // property of TTD, not of the card): those positions are not compared
        const Moment& live = stateAt.at(f);
        ASSERT_TRUE(_ttd->SeekTo({f, live.t}));
        if (_context->pCore->GetZ80()->t != live.t)
        {
            ++inexact;
            continue;
        }
        CatchUp();
        EXPECT_EQ(_card->TtdStateHash(), live.cardState) << "card / chip state";
        EXPECT_EQ(_card->TtdMemoryHash(), live.chipMemory) << "chip memory";
        EveFrameMetrics metrics{};
        EveGetFrameMetrics(_card->Chip(), &metrics, nullptr, 0);
        EXPECT_EQ(metrics.frame, live.metricsFrame) << "metrics block of that moment";
    }
    EXPECT_LE(inexact, 2) << "most positions must be reached exactly";
}

/// Seek to a T-state inside a frame: the FT812 frame drawn up to there over its previous frame
TEST_F(TTDVdac2_Test, SeekInsideFrameShowsTheFrameDrawnSoFar)
{
    struct Probe
    {
        ttd::TTDTimePoint at;
        Moment live;
    };
    std::vector<Probe> probes;
    int differFromLastFinished = 0;

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, true);
    for (uint32_t t : {20000u, 120000u, 230000u})  // 14 MHz frame: 4 x 71680 T-states
    {
        _emulator->RunTStates(t, true);
        const std::vector<uint32_t> lastFinished = Shown();
        // What the rule shows at this moment on a live machine: the frame in flight
        _card->TTDPrepareComposedPicture(false);
        if (!(Shown() == lastFinished))
            ++differFromLastFinished;
        const Moment live = Now();
        probes.push_back({{_context->emulatorState.frame_counter, live.t}, live});
        _emulator->RunNFrames(1, true);
    }
    _emulator->RunNFrames(1, true);
    _ttd->StopRecording();
    EXPECT_GT(differFromLastFinished, 0) << "mid-frame pictures must differ from the last finished frame";

    for (const Probe& p : probes)
    {
        SCOPED_TRACE(p.at.tInFrame);
        ASSERT_TRUE(_ttd->SeekTo(p.at));
        ASSERT_EQ(_context->pCore->GetZ80()->t, p.at.tInFrame) << "a mid-frame position is reached exactly";
        EXPECT_TRUE(Shown() == p.live.picture) << "position (" << p.at.frame << ", " << p.at.tInFrame << ")";
        CatchUp();
        EXPECT_EQ(_card->TtdStateHash(), p.live.cardState);
        EXPECT_EQ(_card->TtdMemoryHash(), p.live.chipMemory);
    }
}

/// A session written to a stream and read back seeks to the same pictures and state
TEST_F(TTDVdac2_Test, SavedSessionReplaysTheSame)
{
    std::map<uint64_t, Moment> stateAt;
    std::map<uint64_t, std::vector<uint32_t>> finalPicture;
    Record(6, stateAt, finalPicture);

    std::stringstream file;
    std::string error;
    ASSERT_TRUE(_ttd->SerializeSession(file, error)) << error;
    file.seekg(0);
    ASSERT_TRUE(_ttd->DeserializeSession(file, error)) << error;

    for (uint64_t f = finalPicture.begin()->first + 1; f <= finalPicture.rbegin()->first; ++f)
    {
        SCOPED_TRACE(f);
        ASSERT_TRUE(_ttd->SeekTo(_ttd->FrameEndPosition(f)));   // by frame number: its end (D13)
        EXPECT_TRUE(Shown() == finalPicture.at(f));
        ASSERT_TRUE(_ttd->SeekTo({f, stateAt.at(f).t}));
        if (_context->pCore->GetZ80()->t != stateAt.at(f).t)
            continue;  // landed on a later instruction boundary (see SeekByFrameMatchesTheLiveRun)
        CatchUp();
        EXPECT_EQ(_card->TtdStateHash(), stateAt.at(f).cardState);
        EXPECT_EQ(_card->TtdMemoryHash(), stateAt.at(f).chipMemory);
    }
}

/// With the history limit: the FT812 blobs go with the evicted checkpoints, and what
/// stays (also after saving and loading) seeks to the live pictures and state
TEST_F(TTDVdac2_Test, HistoryLimitKeepsTheChipRight)
{
    _ttd->SetHistoryLimit(6, 0);
    std::map<uint64_t, Moment> stateAt;
    std::map<uint64_t, std::vector<uint32_t>> finalPicture;
    Record(16, stateAt, finalPicture);
    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    ASSERT_GE(info.checkpointCount, 6u);
    ASSERT_LE(info.checkpointCount, 6u + 1u) << "whole segments covering at least the window (one frame each here)";
    ASSERT_GT(info.evictedCheckpoints, 0u);

    std::stringstream file;
    std::string error;
    ASSERT_TRUE(_ttd->SerializeSession(file, error)) << error;
    file.seekg(0);
    ASSERT_TRUE(_ttd->DeserializeSession(file, error)) << error;

    for (uint64_t f = info.sessionStartFrame + 2; f <= info.currentEndFrame - 1; ++f)
    {
        SCOPED_TRACE(f);
        ASSERT_TRUE(_ttd->SeekTo(_ttd->FrameEndPosition(f)));   // by frame number: its end (D13)
        EXPECT_TRUE(Shown() == finalPicture.at(f));
        ASSERT_TRUE(_ttd->SeekTo({f, stateAt.at(f).t}));
        if (_context->pCore->GetZ80()->t != stateAt.at(f).t)
            continue;
        CatchUp();
        EXPECT_EQ(_card->TtdStateHash(), stateAt.at(f).cardState);
        EXPECT_EQ(_card->TtdMemoryHash(), stateAt.at(f).chipMemory);
    }
}

/// The FT812 Debug window's source (Vdac2Card::PresentedFrameMetrics) follows the
/// picture on the main screen: a latched frame while running, after a seek by frame
/// number the FT812 frame that finished last by the end of that machine frame, after
/// a seek inside a frame plus the lines of the frame in flight drawn so far - each
/// equal to the live run at the same moment (line-budget-metrics.md §3.3)
TEST_F(TTDVdac2_Test, PresentedMetricsFollowTheScreen)
{
    Vdac2Control::FrameMetrics m;
    _card->SetMeasureAlways(true);
    _emulator->RunNFrames(2, true);
    ASSERT_TRUE(_card->PresentedFrameMetrics(m)) << "a latched frame keeps its metrics while measuring";
    EXPECT_TRUE(m.valid);
    EXPECT_FALSE(m.inFlightKnown);

    struct Probe
    {
        ttd::TTDTimePoint at;
        Vdac2Control::FrameMetrics live;
    };
    std::vector<Probe> inside;
    std::map<uint64_t, uint64_t> blockAtFrameEnd;  // machine frame -> FT812 frame of the block at its end
    ASSERT_TRUE(_ttd->StartRecording());
    // Frame boundaries first (the run is still frame aligned here)
    for (int i = 0; i < 4; ++i)
    {
        _emulator->RunNFrames(1, true);
        ASSERT_LT(_context->pCore->GetZ80()->t, 2000u) << "at a frame boundary (the last instruction overshoots a little)";
        _card->ReadFrameMetrics(m, false, false);
        blockAtFrameEnd[_context->emulatorState.frame_counter - 1] = m.frame;
    }
    // Then moments inside frames
    for (uint32_t t : {30000u, 150000u, 250000u})
    {
        _emulator->RunTStates(t, true);
        Probe probe;
        _card->ReadFrameMetrics(probe.live, true, true);
        probe.at = {_context->emulatorState.frame_counter, _context->pCore->GetZ80()->t};
        inside.push_back(probe);
    }
    _emulator->RunNFrames(1, true);
    _ttd->StopRecording();

    int composite = 0;
    for (const Probe& p : inside)
    {
        SCOPED_TRACE(p.at.tInFrame);
        ASSERT_TRUE(_ttd->SeekTo(p.at));
        if (_context->pCore->GetZ80()->t != p.at.tInFrame)
            continue;  // landed on a later instruction boundary
        ASSERT_TRUE(_card->PresentedFrameMetrics(m));
        EXPECT_EQ(m.frame, p.live.frame) << "the last finished FT812 frame at that moment";
        ASSERT_TRUE(m.inFlightKnown) << "a moment inside a frame shows the frame in flight";
        EXPECT_EQ(m.inFlightLinesPassed, p.live.inFlightLinesPassed);
        EXPECT_EQ(m.inFlightLineClocks, p.live.inFlightLineClocks);
        EXPECT_EQ(m.lineClocks, p.live.lineClocks);
        composite += m.inFlightLinesPassed > 0 && m.inFlightLinesPassed < m.lines;
    }
    EXPECT_GT(composite, 0) << "at least one probe sits part way through an FT812 frame";

    // The session's first frame has no checkpoint a frame earlier for the FT812's
    // lead-in (TTDLeadInFrames), so its frame-target picture is not composed; from
    // the second frame on it is
    blockAtFrameEnd.erase(blockAtFrameEnd.begin());
    for (const auto& [frame, ft812Frame] : blockAtFrameEnd)
    {
        SCOPED_TRACE(frame);
        ASSERT_TRUE(_ttd->SeekTo(_ttd->FrameEndPosition(frame)));   // a frame target: its end (D13)
        ASSERT_TRUE(_card->PresentedFrameMetrics(m));
        EXPECT_FALSE(m.inFlightKnown) << "a frame target shows a finished FT812 frame";
        EXPECT_EQ(m.frame, ft812Frame) << "the one that finished last by the end of the machine frame";
    }
}

#endif  // ENABLE_VDAC2
