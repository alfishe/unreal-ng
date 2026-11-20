/// @file ttdcontrol_pastreads_test.cpp
/// @brief PLAN #122 S1, V2, E4 (docs/inprogress/2026-11-12-re-api-efficiency): reads of the past that do not move
/// the machine. memory-series equals memory-at frame by frame and the bytes the machine held live at each frame start;
/// screen-at equals the picture and the screens captured live when the frame ended; memory-at states the moment it
/// read. The guest counts at #5B00, writes the count over the bitmap and flips the shown screen (#7FFD bit 3) every
/// 64 loops, so every frame differs and the shown screen changes inside a frame.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/base64.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"
#include "emulator/video/screenshotter.h"

using ttd::TTDControl;
using ttd::TTDControlError;
using ttd::TTDReply;

class TTDControl_PastReads_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _controller = nullptr;

    /// Live, after each recorded frame F ended: the bytes at the start of F + 1, F's picture and its screens
    struct LiveFrame
    {
        std::vector<uint8_t> counter;   ///< cpu #5B00..#5B01 at the start of the frame
        std::vector<uint8_t> bitmap;    ///< cpu #4000..#403F at the start of the frame
        std::vector<uint8_t> png;       ///< the frame before's final picture (full area)
        std::vector<uint8_t> page5;     ///< 6912 bytes of RAM page 5 at the start of the frame
        std::vector<uint8_t> page7;
        uint8_t p7FFD = 0;
    };
    std::map<uint64_t, LiveFrame> _live;   ///< by the frame whose start the bytes are

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _controller = _context->pTimeTravelController;
        ASSERT_NE(_controller, nullptr);
        FeatureManager* fm = _emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();

        Memory* memory = _context->pMemory;
        // DI; LD HL,#4000
        // loop: LD A,(#5B00); INC A; LD (#5B00),A; LD (HL),A; INC HL; AND #3F; JR NZ,skip
        //       LD A,(#5B01); XOR 8; LD (#5B01),A; LD BC,#7FFD; OUT (C),A
        // skip: LD A,H; CP #58; JR C,cont; LD H,#40
        // cont: JR loop
        const uint8_t program[] = {0xF3, 0x21, 0x00, 0x40, 0x3A, 0x00, 0x5B, 0x3C, 0x32, 0x00, 0x5B, 0x77,
                                   0x23, 0xE6, 0x3F, 0x20, 0x0D, 0x3A, 0x01, 0x5B, 0xEE, 0x08, 0x32, 0x01,
                                   0x5B, 0x01, 0xFD, 0x7F, 0xED, 0x79, 0x7C, 0xFE, 0x58, 0x38, 0x02, 0x26,
                                   0x40, 0x18, 0xDD};
        for (size_t i = 0; i < sizeof(program); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        // Both screens visible: white paper on page 5, cyan on page 7 with a pattern of its own
        uint8_t* page5 = memory->RAMPageAddress(5);
        uint8_t* page7 = memory->RAMPageAddress(7);
        ASSERT_NE(page5, nullptr);
        ASSERT_NE(page7, nullptr);
        for (size_t i = 0; i < 6144; ++i)
        {
            page5[i] = 0;
            page7[i] = static_cast<uint8_t>(i * 7);
        }
        for (size_t i = 6144; i < 6912; ++i)
        {
            page5[i] = 0x38;
            page7[i] = 0x28;
        }
        page5[0x1B00] = 0;
        page5[0x1B01] = 0;
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0xBFF0;
    }

    void TearDown() override
    {
        if (_controller)
            _controller->StopRecording();
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return TTDControl(_context).Execute({verb, std::move(options)});
    }

    bool Shadow() const { return Screen::HasShadowScreen(_context->config.mem_model); }

    LiveFrame CaptureLive()
    {
        LiveFrame f;
        Memory* memory = _context->pMemory;
        for (uint16_t a = 0x5B00; a < 0x5B02; ++a)
            f.counter.push_back(memory->DirectReadFromZ80Memory(a));
        for (uint16_t a = 0x4000; a < 0x4040; ++a)
            f.bitmap.push_back(memory->DirectReadFromZ80Memory(a));
        f.page5.assign(memory->RAMPageAddress(5), memory->RAMPageAddress(5) + 6912);
        f.page7.assign(memory->RAMPageAddress(7), memory->RAMPageAddress(7) + 6912);
        f.p7FFD = _context->emulatorState.p7FFD;
        FrameSnapshot snapshot;
        if (_context->pScreen->SnapshotLive(snapshot, /*emulationParked=*/true, /*timeoutMs=*/0))
        {
            const ScreenshotResult shot = Screenshotter::Render(snapshot, ScreenshotOptions{});
            if (shot.ok)
                f.png = shot.bytes;
        }
        return f;
    }

    /// Records @p frames frames, keeping the live state after each
    void Record(int frames)
    {
        ASSERT_TRUE(Run("start").Ok());
        // The recording's first frame starts here
        _live[_context->emulatorState.frame_counter] = CaptureLive();
        for (int i = 0; i < frames; ++i)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            _live[_context->emulatorState.frame_counter] = CaptureLive();
        }
        ASSERT_TRUE(Run("stop").Ok());
    }

    static std::vector<uint8_t> Unhex(const std::string& hex)
    {
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        return out;
    }
    static std::vector<uint8_t> Unbase64(const std::string& text)
    {
        std::vector<uint8_t> out;
        EXPECT_TRUE(base64::Decode(text, out));
        return out;
    }
    /// A RAM page's screen (6912 bytes) at a frame's start, as memory-at reads it
    std::vector<uint8_t> RamAt(int page, uint64_t frame)
    {
        const TTDReply r = Run("memory-at", {{"space", "ram" + std::to_string(page)}, {"frame", std::to_string(frame)},
                                             {"offset", "0"}, {"length", "6912"}});
        EXPECT_TRUE(r.Ok()) << r.message;
        return Unhex(Str(r.body, "hex"));
    }
    static size_t Differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        size_t n = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            n += a[i] != b[i] ? 1 : 0;
        return n;
    }
    static int64_t Int(const StateNode& n, const char* key)
    {
        const StateNode* v = n.find(key);
        return v ? v->i : -1;
    }
    static std::string Str(const StateNode& n, const char* key)
    {
        const StateNode* v = n.find(key);
        return v ? v->s : std::string("<missing>");
    }
};

TEST_F(TTDControl_PastReads_Test, SeriesEqualsMemoryAtAndTheLiveBytesFrameByFrame)
{
    Record(10);
    const uint64_t first = _live.begin()->first;
    const uint64_t last = _live.rbegin()->first;
    const TTDReply r = Run("memory-series", {{"ranges", "0x5B00:2, ram5:0x1B00:2, cpu:0x4000:64, ram7:0:16"},
                                             {"from_frame", std::to_string(first)},
                                             {"to_frame", std::to_string(last)}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r.body, "moment"), "frame_start");
    EXPECT_EQ(Int(r.body, "restored_frames"), 0) << "a recorded session reads cpu ranges from the store";
    const std::vector<StateNode>& rows = r.body.find("rows")->items;
    ASSERT_EQ(rows.size(), last - first + 1);
    // One walk: a piece not changed since the frame before is not decoded again
    EXPECT_LT(Int(r.body, "pieces_decoded"), static_cast<int64_t>(rows.size() * 4));
    for (const StateNode& row : rows)
    {
        const uint64_t frame = static_cast<uint64_t>(Int(row, "frame"));
        SCOPED_TRACE("frame " + std::to_string(frame));
        EXPECT_TRUE(row.find("exact")->b);
        const std::vector<StateNode>& data = row.find("data")->items;
        ASSERT_EQ(data.size(), 4u);
        // The live oracle: the machine at that frame's start
        const LiveFrame& live = _live.at(frame);
        EXPECT_EQ(Unbase64(data[0].s), live.counter);
        EXPECT_EQ(Unbase64(data[1].s), live.counter);
        EXPECT_EQ(Unbase64(data[2].s), live.bitmap);
        EXPECT_EQ(Unbase64(data[3].s), std::vector<uint8_t>(live.page7.begin(), live.page7.begin() + 16));
        // The per-frame oracle: memory-at at the same frame
        const TTDReply at = Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(frame)}, {"offset", "0x4000"},
                                              {"length", "64"}});
        ASSERT_TRUE(at.Ok()) << at.message;
        EXPECT_EQ(Unhex(Str(at.body, "hex")), Unbase64(data[2].s));
        const TTDReply ram = Run("memory-at", {{"space", "ram5"}, {"frame", std::to_string(frame)}, {"offset", "0x1B00"},
                                               {"length", "2"}});
        ASSERT_TRUE(ram.Ok()) << ram.message;
        EXPECT_EQ(Unhex(Str(ram.body, "hex")), Unbase64(data[1].s));
        // E4: the same moment named by both
        EXPECT_EQ(Int(row, "at_tinframe"), Int(ram.body, "at_tinframe"));
    }
    // Every frame differs: the counter runs
    EXPECT_NE(Unbase64(rows.front().find("data")->items[0].s), Unbase64(rows.back().find("data")->items[0].s));
}

TEST_F(TTDControl_PastReads_Test, SeriesStepListChangesOnlyAndFormats)
{
    Record(9);
    const uint64_t first = _live.begin()->first;
    const uint64_t last = _live.rbegin()->first;

    // A step: every third frame from the first
    TTDReply r = Run("memory-series", {{"ranges", "0x5B00:1"}, {"step", "3"}, {"format", "values"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Int(r.body, "from_frame"), static_cast<int64_t>(first));
    EXPECT_EQ(Int(r.body, "to_frame"), static_cast<int64_t>(last));
    std::vector<uint64_t> frames;
    for (const StateNode& row : r.body.find("rows")->items)
    {
        frames.push_back(static_cast<uint64_t>(Int(row, "frame")));
        EXPECT_EQ(row.find("data")->items[0].items[0].i, _live.at(frames.back()).counter[0]);
    }
    std::vector<uint64_t> expected;
    for (uint64_t f = first; f <= last; f += 3)
        expected.push_back(f);
    EXPECT_EQ(frames, expected);

    // A list, in hex; a JSON array of objects names the same bytes
    const std::string list = std::to_string(first + 1) + "," + std::to_string(last);
    r = Run("memory-series", {{"ranges", "[{\"page\": 5, \"offset\": \"0x1B00\", \"length\": 2}]"}, {"frames", list},
                              {"format", "hex"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    ASSERT_EQ(r.body.find("rows")->items.size(), 2u);
    EXPECT_EQ(Unhex(r.body.find("rows")->items[1].find("data")->items[0].s), _live.at(last).counter);

    // changes_only: the first frame, then only the frames where a range changed. The bitmap's first bytes change
    // only while the writes pass them; the counter every frame
    r = Run("memory-series", {{"ranges", "0x5B00:1, 0x5B01:1"}, {"changes_only", "true"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    const std::vector<StateNode>& rows = r.body.find("rows")->items;
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(Int(rows[0], "frame"), static_cast<int64_t>(first));
    EXPECT_EQ(rows[0].find("changed"), nullptr);
    std::vector<uint8_t> previous = _live.at(first).counter;
    size_t expectedRows = 1;
    for (uint64_t f = first + 1; f <= last; ++f)
        if (_live.at(f).counter != _live.at(f - 1).counter)
            ++expectedRows;
    EXPECT_EQ(rows.size(), expectedRows);
    for (size_t i = 1; i < rows.size(); ++i)
    {
        const uint64_t f = static_cast<uint64_t>(Int(rows[i], "frame"));
        const std::vector<uint8_t>& now = _live.at(f).counter;
        const std::vector<uint8_t>& before = _live.at(f - 1).counter;
        ASSERT_NE(rows[i].find("changed"), nullptr);
        std::vector<int64_t> changed;
        for (const StateNode& c : rows[i].find("changed")->items)
            changed.push_back(c.i);
        std::vector<int64_t> want;
        if (now[0] != before[0])
            want.push_back(0);
        if (now[1] != before[1])
            want.push_back(1);
        EXPECT_EQ(changed, want) << "frame " << f;
    }
}

TEST_F(TTDControl_PastReads_Test, SeriesLimitsAndRefusals)
{
    Record(8);
    const uint64_t first = _live.begin()->first;
    const uint64_t last = _live.rbegin()->first;

    // Rows: past the limit the answer says where to go on
    TTDReply r = Run("memory-series", {{"ranges", "0x5B00:2"}, {"limit", "3"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("rows")->items.size(), 3u);
    EXPECT_TRUE(r.body.find("truncated")->b);
    EXPECT_EQ(Int(r.body, "next_from_frame"), static_cast<int64_t>(first + 3));
    // Bytes: 4 rows of 2 bytes fit in 8 bytes
    r = Run("memory-series", {{"ranges", "0x5B00:2"}, {"max_bytes", "8"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("rows")->items.size(), 4u);
    EXPECT_EQ(Int(r.body, "next_from_frame"), static_cast<int64_t>(first + 4));
    // One row always fits
    r = Run("memory-series", {{"ranges", "0x4000:64"}, {"max_bytes", "1"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("rows")->items.size(), 1u);
    EXPECT_FALSE(Run("memory-series", {{"ranges", "0x5B00:2"}, {"limit", "100"}}).body.find("truncated")->b);

    auto refused = [&](std::map<std::string, std::string> options, const char* text) {
        const TTDReply bad = Run("memory-series", options);
        EXPECT_EQ(bad.error, TTDControlError::BadRequest) << text;
        EXPECT_NE(bad.message.find(text), std::string::npos) << bad.message;
    };
    refused({}, "needs ranges");
    refused({{"ranges", "0x5B00"}}, "Invalid range");
    refused({{"ranges", "0xFFF0:32"}}, "Z80 addresses end at 0xFFFF");
    refused({{"ranges", "nosuch:0:1"}}, "is not a memory this session records");
    refused({{"ranges", "0x4000:40000, 0x4000:40000"}}, "bytes per frame");
    std::string many;
    for (int i = 0; i < 17; ++i)
        many += "0x4000:1,";
    refused({{"ranges", many}}, "at most 16");
    refused({{"ranges", "0x5B00:2"}, {"from_frame", std::to_string(last + 5)}, {"to_frame", std::to_string(last + 6)}},
            "outside the history");
    refused({{"ranges", "0x5B00:2"}, {"frames", "5,4"}}, "ascending");
    refused({{"ranges", "0x5B00:2"}, {"frames", "5"}, {"step", "2"}}, "takes no from_frame");
    refused({{"ranges", "0x5B00:2"}, {"step", "0"}}, "Invalid step");
    refused({{"ranges", "0x5B00:2"}, {"format", "octal"}}, "Invalid format");
    refused({{"ranges", "0x5B00:2"}, {"limit", "0"}}, "Invalid limit");
    EXPECT_EQ(Run("memory-series", {{"ranges", "0x5B00:2"}, {"bogus", "1"}}).error, TTDControlError::BadRequest);

    // No history: refused, not empty
    ASSERT_TRUE(Run("invalidate").Ok());
    EXPECT_FALSE(Run("memory-series", {{"ranges", "0x5B00:2"}}).Ok());
}

TEST_F(TTDControl_PastReads_Test, SeriesOfALoadedSessionRestoresCheckpointsForCpuRanges)
{
    Record(6);
    const TTDReply before = Run("memory-series", {{"ranges", "0x5B00:2, 0x4000:16, ram5:0x1B00:2"}});
    ASSERT_TRUE(before.Ok()) << before.message;
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("pastreads.ttd");
    ASSERT_TRUE(Run("dump", {{"path", path}}).Ok());
    ASSERT_TRUE(Run("invalidate").Ok());
    const TTDReply load = Run("load", {{"path", path}});
    ASSERT_TRUE(load.Ok()) << load.message;
    const std::vector<uint8_t> liveBefore(_context->pMemory->RAMPageAddress(5), _context->pMemory->RAMPageAddress(5) + 0x4000);
    const uint16_t pcBefore = _context->pCore->GetZ80()->pc;

    const TTDReply after = Run("memory-series", {{"ranges", "0x5B00:2, 0x4000:16, ram5:0x1B00:2"}});
    ASSERT_TRUE(after.Ok()) << after.message;
    const std::vector<StateNode>& a = before.body.find("rows")->items;
    const std::vector<StateNode>& b = after.body.find("rows")->items;
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t k = 0; k < 3; ++k)
            EXPECT_EQ(a[i].find("data")->items[k].s, b[i].find("data")->items[k].s) << "row " << i << " range " << k;
    // The machine put back after the restores
    EXPECT_EQ(std::vector<uint8_t>(_context->pMemory->RAMPageAddress(5), _context->pMemory->RAMPageAddress(5) + 0x4000),
              liveBefore);
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pcBefore);
    std::remove(path.c_str());
}

TEST_F(TTDControl_PastReads_Test, ScreenAtEqualsTheLivePictureAndScreens)
{
    if (!Shadow())
        GTEST_SKIP() << "the default model has no shadow screen";
    Record(6);
    const uint64_t first = _live.begin()->first;
    const uint64_t last = _live.rbegin()->first;
    const uint16_t pcBefore = _context->pCore->GetZ80()->pc;
    bool flipsSeen = false;
    for (uint64_t frame = first; frame < last; ++frame)
    {
        SCOPED_TRACE("frame " + std::to_string(frame));
        // The live state after frame F is in _live[F + 1]
        const LiveFrame& end = _live.at(frame + 1);
        const LiveFrame& start = _live.at(frame);
        TTDReply r = Run("screen-at", {{"frame", std::to_string(frame)}});
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(Str(r.body, "mime"), "image/png");
        EXPECT_EQ(Unbase64(Str(r.body, "data")), end.png) << "the shown picture differs from the live one";
        const StateNode* shown = r.body.find("shown");
        EXPECT_EQ(Int(*shown->find("at_start"), "p7ffd"), start.p7FFD);
        EXPECT_EQ(Int(*shown->find("at_end"), "p7ffd"), end.p7FFD);
        EXPECT_EQ(Str(*shown->find("at_end"), "screen"), (end.p7FFD & 0x08) ? "shadow" : "normal");
        flipsSeen = flipsSeen || !shown->find("flips")->items.empty();
        EXPECT_TRUE(shown->find("changed_during_frame")->b) << "the guest flips every 64 loops";

        // The screens as the frame boundary left them: memory-at's bytes at the next frame's start. The live machine
        // stood a few T past the boundary, so the instruction that crossed it may have written one or two bytes more
        const std::vector<uint8_t> normalAtEnd = RamAt(5, frame + 1);
        const std::vector<uint8_t> shadowAtEnd = RamAt(7, frame + 1);
        EXPECT_LE(Differing(normalAtEnd, end.page5) + Differing(shadowAtEnd, end.page7), 2u);
        r = Run("screen-at", {{"frame", std::to_string(frame)}, {"which", "normal"}, {"format", "raw"}});
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(Unbase64(Str(r.body, "data")), normalAtEnd);
        r = Run("screen-at", {{"frame", std::to_string(frame)}, {"which", "shadow"}, {"format", "attributes"},
                              {"encoding", "hex"}});
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(Unhex(Str(r.body, "data")), std::vector<uint8_t>(shadowAtEnd.begin() + 6144, shadowAtEnd.end()));
        r = Run("screen-at", {{"frame", std::to_string(frame)}, {"which", "normal"}, {"format", "bitmap"}});
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(Unbase64(Str(r.body, "data")),
                  std::vector<uint8_t>(normalAtEnd.begin(), normalAtEnd.begin() + 6144));
    }
    EXPECT_TRUE(flipsSeen);
    // A screen's own picture: 256 x 192
    TTDReply r = Run("screen-at", {{"frame", std::to_string(first)}, {"which", "shadow"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Int(r.body, "width"), 256);
    EXPECT_EQ(Int(r.body, "height"), 192);
    // The machine did not move
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pcBefore);

    // Refusals: the last frame has not ended inside the history; bad words
    r = Run("screen-at", {{"frame", std::to_string(last)}});
    EXPECT_EQ(r.error, TTDControlError::BadRequest);
    EXPECT_NE(r.message.find("has not ended"), std::string::npos) << r.message;
    EXPECT_EQ(Run("screen-at", {}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("screen-at", {{"frame", std::to_string(first)}, {"which", "third"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("screen-at", {{"frame", std::to_string(first)}, {"format", "gif"}}).error, TTDControlError::BadRequest);
    EXPECT_EQ(Run("screen-at", {{"frame", std::to_string(first)}, {"which", "normal"}, {"area", "screen"}}).error,
              TTDControlError::BadRequest);
}

TEST_F(TTDControl_PastReads_Test, MemoryAtStatesTheMomentItRead)
{
    Record(4);
    const uint64_t frame = _live.begin()->first + 1;
    ttd::TTDTimePoint moment;
    ASSERT_TRUE(_controller->CheckpointMoment(frame, moment));
    EXPECT_EQ(moment.frame, frame);

    TTDReply r = Run("memory-at", {{"space", "ram"}, {"frame", std::to_string(frame)}, {"length", "4"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r.body, "moment"), "frame_start");
    EXPECT_EQ(Int(r.body, "at_tinframe"), static_cast<int64_t>(moment.tInFrame));
    EXPECT_NE(Str(r.body, "moment_text").find("the start of frame " + std::to_string(frame) + ", before its first instruction"),
              std::string::npos)
        << Str(r.body, "moment_text");

    r = Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(frame)}, {"offset", "0x5B00"}, {"length", "2"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r.body, "moment"), "frame_start");
    EXPECT_EQ(Int(r.body, "at_tinframe"), static_cast<int64_t>(moment.tInFrame));
    EXPECT_EQ(Unhex(Str(r.body, "hex")), _live.at(frame).counter);

    r = Run("memory-at", {{"space", "ram"}, {"frame", std::to_string(frame)}, {"tinframe", "1000"}, {"length", "4"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r.body, "moment"), "inside_frame");
    EXPECT_EQ(Int(r.body, "at_tinframe"), 1000);
    EXPECT_NE(Str(r.body, "moment_text").find("inside frame"), std::string::npos);

    r = Run("memory-at", {{"space", "cpu"}, {"offset", "0x5B00"}, {"length", "2"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(Str(r.body, "moment"), "now");
    EXPECT_NE(Str(r.body, "moment_text").find("the machine now"), std::string::npos);
}
