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
#include <cstring>
#include <map>
#include <random>
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
    EveHostCard* _card = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-vdac2", "TSL-VDAC2", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        ASSERT_NE(decoder, nullptr);
        _card = decoder->GetEveCard();
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

    /// What the monitor shows: the FT812 frame that finished last (the present queue; a direct run's frames are
    /// due at once). The framebuffer (Shown) of a stopped machine holds the frame in flight drawn up to the stop
    std::vector<uint32_t> Presented()
    {
        std::vector<uint32_t> picture(Shown().size());
        EXPECT_TRUE(_context->pScreen->CopyPresentedFramebuffer(reinterpret_cast<uint8_t*>(picture.data()),
                                                                picture.size() * sizeof(uint32_t)));
        return picture;
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
            finalPicture[f - 1] = Presented();
            stateAt[f] = Now();
        }
        _ttd->StopRecording();
    }
};

/// The FT812 is in the checkpoints: its blobs are registered and sized
TEST_F(TTDVdac2_Test, ChipBlobsAreRecorded)
{
    const auto ids = _context->pPortDecoder->GetTTDModelStateTypes();
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::TTDDeviceType::Vdac2Memory), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::TTDDeviceType::Vdac2), ids.end());
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

/// What the monitor shows (the UI, screenshots, the video wall read the present queue, not the
/// framebuffer): after a seek the FT812 picture of that position, and a throwaway replay that composes
/// nothing for the user (a frame-cache build) leaves it alone. Found with Zuma on 2026-10-08: the queue
/// kept the picture from before the seek
TEST_F(TTDVdac2_Test, TheMonitorShowsTheSeekedPicture)
{
    std::map<uint64_t, Moment> stateAt;
    std::map<uint64_t, std::vector<uint32_t>> finalPicture;
    Record(10, stateAt, finalPicture);
    auto presented = [this]() { return Presented(); };

    const uint64_t first = finalPicture.begin()->first + 1;
    const uint64_t last = finalPicture.rbegin()->first;
    for (uint64_t f : {last, first, first + 4, first + 1})
    {
        SCOPED_TRACE(f);
        ASSERT_TRUE(_ttd->SeekTo(_ttd->FrameEndPosition(f)));
        ASSERT_TRUE(presented() == finalPicture.at(f)) << "the monitor shows the picture at the end of frame " << f;
        // Frame-cache builds replay whole frames (FT812 frames end in them) and show nothing; more
        // of them than the present queue delays, so a frame they latched would reach the monitor
        for (uint64_t other = first; other <= last; ++other)
            ASSERT_NE(_ttd->GetFrameCache(other), nullptr);
        EXPECT_TRUE(presented() == finalPicture.at(f)) << "still frame " << f << " after frame-cache builds";
    }
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
        const std::vector<uint32_t> lastFinished = Presented();
        // What the rule shows at this moment: the frame in flight. The run's end shows it already
        const std::vector<uint32_t> atStop = Shown();
        _card->TTDPrepareComposedPicture(false);
        EXPECT_TRUE(Shown() == atStop) << "a stopped machine shows the FT812 frame in flight drawn so far";
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

namespace
{
/// A Z80 program for the FT812 through #77 / #57 (vdac2cardfixture.h): `setup` (address, 32-bit value pairs) once,
/// then a 16 x 16 RGB565 bitmap at RAM_G 0 rewritten without a pause, one color per pass (about 0.8 ms at 14 MHz).
/// The display list of `setup` repeats that bitmap over the whole 1024 x 768 picture: every FT812 line shows the
/// color of the moment it was drawn, so a line drawn at another moment (from another input) differs
std::vector<uint8_t> RamGWriterProgram(const std::vector<std::pair<uint32_t, uint32_t>>& setup)
{
    std::vector<uint8_t> code = {0xF3, 0x06, 0x00};  // DI; LD B,0 (OUT (C),A: port #00xx)
    auto out = [&code](uint8_t port, uint8_t value) {
        code.insert(code.end(), {0x0E, port, 0x3E, value, 0xED, 0x79});  // LD C,port; LD A,value; OUT (C),A
    };
    auto writeHeader = [&](uint32_t address) {
        out(0x77, 0x06);  // FT812 selected
        out(0x57, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        out(0x57, static_cast<uint8_t>(address >> 8));
        out(0x57, static_cast<uint8_t>(address));
    };
    for (const auto& [address, value] : setup)
    {
        writeHeader(address);
        for (int i = 0; i < 4; ++i)
            out(0x57, static_cast<uint8_t>(value >> (8 * i)));
        out(0x77, 0x02);  // deselected
    }
    code.insert(code.end(), {0x1E, 0x00});  // LD E,0: the color
    const size_t loop = code.size();
    writeHeader(0);
    code.insert(code.end(), {0x16, 0x00});  // LD D,0: 256 pixels
    const size_t inner = code.size();
    code.insert(code.end(), {0x7B, 0xED, 0x79, 0xED, 0x79, 0x15});  // LD A,E; OUT (C),A twice; DEC D
    code.insert(code.end(), {0x20, static_cast<uint8_t>(inner - (code.size() + 2))});  // JR NZ,inner
    out(0x77, 0x02);
    code.push_back(0x1C);  // INC E
    code.insert(code.end(), {0x18, static_cast<uint8_t>(loop - (code.size() + 2))});  // JR loop
    return code;
}
}  // namespace

/// Inside a frame the picture holds two FT812 frames, the one in flight and the one under it, and either may
/// have started before the checkpoint of the frame before the position (an FT812 frame is 16.9 ms, a machine
/// frame 20.5 ms). A replay draws again from line 0 the FT812 frame in flight at its checkpoint, with the
/// inputs of the checkpoint: lines of it the picture shows were then not the ones the live machine drew. And the
/// live machine, stopped there, showed the FT812 frame that finished last instead of the frame in flight. Found
/// with Metal Slug on VDAC4 (it writes RAM_G all the time), 2026-11-27: some seeks showed another picture than the
/// live machine at the same position while the state was exact. Every position, at every FT812 phase, shows the
/// live picture; one in the session's first frame, which no replay can draw, says so in the check
TEST_F(TTDVdac2_Test, SeekShowsTheLivePictureWhileRamGChanges)
{
    // The program stops test6 (its own drawing would overwrite the display list); a frame first lets the
    // coprocessor finish what test6 queued
    auto start = [this](uint16_t at, const std::vector<uint8_t>& code) {
        for (size_t i = 0; i < code.size(); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(at + i), code[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = at;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;  // test6 waits for its interrupt in HALT
    };
    start(0x8000, {0xF3, 0x18, 0xFE});  // DI; JR $
    _emulator->RunNFrames(2, true);
    constexpr uint32_t kRamDl = 0x300000;
    start(0x8000, RamGWriterProgram({
        {kRamDl + 0, 0x02000000},                                    // CLEAR_COLOR_RGB(0, 0, 0)
        {kRamDl + 4, 0x26000007},                                    // CLEAR(1, 1, 1)
        {kRamDl + 8, 0x04FFFFFF},                                    // COLOR_RGB(255, 255, 255)
        {kRamDl + 12, 0x01000000},                                   // BITMAP_SOURCE(0)
        {kRamDl + 16, 0x07000000 | (7u << 19) | (32u << 9) | 16u},   // BITMAP_LAYOUT(RGB565, 32, 16)
        {kRamDl + 20, 0x28000000},                                   // BITMAP_LAYOUT_H(0, 0)
        {kRamDl + 24, 0x08000000 | (1u << 19) | (1u << 18) | 256u},  // BITMAP_SIZE(NEAREST, REPEAT, REPEAT, 1024 & 511, 768 & 511)
        {kRamDl + 28, 0x29000000 | (2u << 2) | 1u},                  // BITMAP_SIZE_H(1024 >> 9, 768 >> 9)
        {kRamDl + 32, 0x1F000001},                                   // BEGIN(BITMAPS)
        {kRamDl + 36, 0x80000000},                                   // VERTEX2II(0, 0, 0, 0)
        {kRamDl + 40, 0x21000000},                                   // END
        {kRamDl + 44, 0x00000000},                                   // DISPLAY
        {0x302054, 2},                                               // REG_DLSWAP: at the frame end
    }));
    _emulator->RunNFrames(2, true);
    ASSERT_TRUE(_card->IsShowing());

    struct Probe
    {
        ttd::TTDTimePoint at;
        std::vector<uint32_t> live;
    };
    std::vector<Probe> probes;
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t firstFrame = _context->emulatorState.frame_counter;
    // In the session's first frame: no checkpoint before it
    _emulator->RunTStates(20000, true);
    const ttd::TTDTimePoint inFirstFrame{_context->emulatorState.frame_counter,
                                         static_cast<uint32_t>(_context->pCore->GetZ80()->t)};
    ASSERT_EQ(inFirstFrame.frame, firstFrame);
    _emulator->RunNFrames(2, true);
    // 14 MHz frame: 4 x 71680 T-states. Positions early in a frame and late, at drifting FT812 phases
    for (uint32_t t : {3000u, 150000u, 9000u, 270000u, 30000u, 60000u, 5000u, 90000u, 15000u, 1000u})
    {
        _emulator->RunTStates(t, true);
        // What the rule shows at this moment on a live machine: the FT812 frame in flight over the one before.
        // The stopped machine shows it already (Screen::CatchUpExternalPicture)
        const std::vector<uint32_t> atStop = Shown();
        _card->TTDPrepareComposedPicture(false);
        EXPECT_TRUE(Shown() == atStop) << "the live machine stopped at (" << _context->emulatorState.frame_counter
                                       << ", " << _context->pCore->GetZ80()->t << ") shows the frame in flight";
        probes.push_back(
            {{_context->emulatorState.frame_counter, static_cast<uint32_t>(_context->pCore->GetZ80()->t)}, Shown()});
        _emulator->RunNFrames(1, true);
    }
    _ttd->StopRecording();
    std::set<std::vector<uint32_t>> distinct;
    for (const Probe& p : probes)
        distinct.insert(p.live);
    ASSERT_GE(distinct.size(), probes.size() - 1) << "the pictures must differ, or a seek proves nothing";

    // Back and forth: every seek restores against another live state
    std::vector<size_t> order;
    for (size_t i = 0; i < probes.size(); ++i)
        order.push_back(i % 2 ? probes.size() - 1 - i / 2 : i / 2);
    for (size_t i : order)
    {
        const Probe& p = probes[i];
        SCOPED_TRACE(::testing::Message() << "position (" << p.at.frame << ", " << p.at.tInFrame << ")");
        ASSERT_TRUE(_ttd->SeekTo(p.at));
        ASSERT_EQ(_context->pCore->GetZ80()->t, p.at.tInFrame);
        EXPECT_TRUE(Shown() == p.live) << "the picture of the live machine at that moment";
        EXPECT_EQ(_ttd->LastEngineCheck().status, ttd::TTDRestoreStatus::Exact) << _ttd->LastEngineCheck().message;
    }

    // The session's first frame: what the FT812 drew before the session started is in no checkpoint
    ASSERT_TRUE(_ttd->SeekTo(inFirstFrame));
    const ttd::TTDRestoreResult& check = _ttd->LastEngineCheck();
    EXPECT_EQ(check.status, ttd::TTDRestoreStatus::NotBitExact);
    EXPECT_TRUE(std::any_of(check.issues.begin(), check.issues.end(), [](const ttd::TTDRestoreIssue& issue) {
        return issue.kind == ttd::TTDRestoreIssueKind::PictureIncomplete;
    })) << check.message;
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

namespace
{
/// The memory blob's region encoding as it was written first, a byte at a time: the reference the word scan must
/// match byte for byte (the blob is stored: the stream may not change)
std::vector<uint8_t> ZeroRunsByteByByte(const std::vector<uint8_t>& region)
{
    std::vector<uint8_t> out;
    auto token = [&](uint32_t t) {
        const size_t at = out.size();
        out.resize(at + 4);
        std::memcpy(out.data() + at, &t, 4);
    };
    const uint8_t* p = region.data();
    const size_t size = region.size();
    size_t pos = 0, dataStart = 0;
    auto flush = [&](size_t end) {
        if (end > dataStart)
        {
            token(static_cast<uint32_t>(end - dataStart));
            out.insert(out.end(), p + dataStart, p + end);
        }
    };
    while (pos < size)
    {
        if (p[pos] != 0)
        {
            ++pos;
            continue;
        }
        size_t run = 0;
        while (pos + run < size && p[pos + run] == 0)
            ++run;
        if (run >= 64)
        {
            flush(pos);
            token(0x8000'0000u | static_cast<uint32_t>(run));
            pos += run;
            dataStart = pos;
        }
        else
            pos += run;
    }
    flush(size);
    return out;
}
}  // namespace

/// The chip memory blob's word scan writes what the byte scan wrote: zero runs around 64 bytes at every
/// alignment, at the region's ends, short runs next to long ones, odd sizes, all zero, none zero
TEST(TTDVdac2ZeroRuns_Test, TheWordScanWritesWhatTheByteScanWrote)
{
    std::mt19937 rng(5);
    std::vector<std::vector<uint8_t>> regions = {{}, std::vector<uint8_t>(1, 0), std::vector<uint8_t>(63, 0),
                                                 std::vector<uint8_t>(64, 0), std::vector<uint8_t>(4096, 0),
                                                 std::vector<uint8_t>(4097, 7)};
    for (int i = 0; i < 400; ++i)
    {
        std::vector<uint8_t> r(1 + rng() % 3000);
        for (uint8_t& b : r)
            b = static_cast<uint8_t>(1 + rng() % 255);
        for (int runs = static_cast<int>(rng() % 12); runs > 0; --runs)
        {
            const size_t len = (rng() % 4 == 0) ? 56 + rng() % 17 : rng() % 200;   // around 64 often
            const size_t at = rng() % r.size();
            std::fill(r.begin() + static_cast<long>(at), r.begin() + static_cast<long>(std::min(r.size(), at + len)), 0);
        }
        if (i % 7 == 0)
            std::fill(r.begin(), r.begin() + static_cast<long>(std::min<size_t>(r.size(), 64 + i % 9)), 0);
        if (i % 11 == 0)
            std::fill(r.end() - static_cast<long>(std::min<size_t>(r.size(), 60 + i % 13)), r.end(), 0);
        regions.push_back(std::move(r));
    }
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const std::vector<uint8_t>& r = regions[i];
        std::vector<uint8_t> out(r.size() + 16);
        const uint8_t* end = EveHostCard::TtdEncodeZeroRuns(r.data(), r.size(), out.data());
        out.resize(static_cast<size_t>(end - out.data()));
        ASSERT_EQ(out, ZeroRunsByteByByte(r)) << "region " << i << ", " << r.size() << " bytes";
    }
}

#endif  // ENABLE_VDAC2
