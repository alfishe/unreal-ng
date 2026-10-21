// The published TTD cursor (control surfaces architecture §7): a seqlock readers never see torn, written by both
// backends at frame boundaries and at the end of every operation, and the position verb answered from it

#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

using ttd::TTDCursor;
using ttd::TTDCursorCell;
using ttd::TTDSessionState;

TEST(TTDCursorCell_Test, ReadersNeverSeeATornCursorWhileItIsWritten)
{
    // Every value written keeps frame == first + 7 == end - 3 and tinframe == endTinframe: a torn read breaks it
    TTDCursorCell cell;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> writes{0};
    std::thread writer([&] {
        for (uint64_t n = 1; !stop.load(std::memory_order_relaxed); n++)
        {
            TTDCursor c;
            c.first = n * 1000003u;
            c.frame = c.first + 7;
            c.end = c.frame + 3;
            c.tinframe = static_cast<uint32_t>(n);
            c.endTinframe = static_cast<uint32_t>(n);
            c.state = (n & 1) ? TTDSessionState::Recording : TTDSessionState::Detached;
            c.history = (n & 1) != 0;
            cell.Publish(c);
            writes.fetch_add(1, std::memory_order_relaxed);
        }
    });
    uint64_t reads = 0;
    uint64_t torn = 0;
    // A bounded number of reads beside the writer (well under the 50 ms budget)
    while (reads < 200000)
    {
        const TTDCursor c = cell.Read();
        reads++;
        if (c.first == 0)
            continue;  // before the first write
        const bool odd = (c.tinframe & 1u) != 0;
        if (c.frame != c.first + 7 || c.end != c.frame + 3 || c.tinframe != c.endTinframe ||
            (c.state == TTDSessionState::Recording) != odd || c.history != odd)
            torn++;
    }
    stop = true;
    writer.join();
    EXPECT_EQ(torn, 0u) << "of " << reads << " reads beside " << writes.load() << " writes";
    EXPECT_GT(writes.load(), 0u);
}

TEST(TTDCursorCell_Test, AnEqualCursorDoesNotMoveTheSequence)
{
    TTDCursorCell cell;
    TTDCursor c;
    c.frame = 10;
    cell.Publish(c);
    const uint64_t after = cell.Sequence();
    EXPECT_EQ(after % 2, 0u);
    c.paused = true;  // not stored: read live
    cell.Publish(c);
    EXPECT_EQ(cell.Sequence(), after);
    c.frame = 11;
    cell.Publish(c);
    EXPECT_EQ(cell.Sequence(), after + 2);
    EXPECT_EQ(cell.Read().frame, 11u);
    EXPECT_FALSE(cell.Read().paused);
}

/// Both backends (v1's TimeTravelManager and the engine's TimeTravelController), wired as ttdcontrol_test does
class TTDCursor_Test : public ::testing::TestWithParam<bool>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::ITimeTravelHooks* _ttd = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        if (GetParam())
        {
            _controller = std::make_unique<ttd::TimeTravelController>(_context);
            _context->pTimeTravelHooks = _controller.get();
            _context->ttdWriteSink = _controller.get();
            _context->pTimeTravelController = _controller.get();
        }
        _ttd = _context->pTimeTravelHooks;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_controller)
        {
            _controller->StopRecording();
            _context->pTimeTravelHooks = _context->pTimeTravelManager;
            _context->ttdWriteSink = _context->pTimeTravelManager;
            _context->pTimeTravelController = nullptr;
            _controller.reset();
        }
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }
};

INSTANTIATE_TEST_SUITE_P(Backends, TTDCursor_Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Engine" : "V1"; });

TEST_P(TTDCursor_Test, TheCursorFollowsRecordingStopSeekAndSteps)
{
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    TTDCursor c = _ttd->PublishedCursor();
    EXPECT_FALSE(c.history);
    EXPECT_EQ(c.state, TTDSessionState::Idle);
    EXPECT_EQ(c.frame, _context->emulatorState.frame_counter) << "a frame boundary publishes without a session too";

    ASSERT_TRUE(Run("start").Ok());
    _emulator->RunNFrames(6, /*skipBreakpoints=*/true);
    c = _ttd->PublishedCursor();
    EXPECT_TRUE(c.history);
    EXPECT_EQ(c.state, TTDSessionState::Recording);
    EXPECT_EQ(c.frame, _context->emulatorState.frame_counter);
    EXPECT_GE(c.end, c.first + 5);

    ASSERT_TRUE(Run("stop").Ok());
    c = _ttd->PublishedCursor();
    const uint64_t first = c.first;
    const uint64_t end = c.end;
    EXPECT_EQ(c.state, TTDSessionState::Idle);
    EXPECT_TRUE(c.history);

    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(first + 2)}, {"tinframe", "0"}}).Ok());
    c = _ttd->PublishedCursor();
    EXPECT_EQ(c.frame, first + 2) << "the seek's end publishes where it left the machine";
    // At the frame's start, to the instruction the machine stopped on (the live position, as the verb answers it)
    EXPECT_EQ(static_cast<int64_t>(c.tinframe), Run("position").body.find("current")->find("tinframe")->i);
    EXPECT_EQ(c.state, TTDSessionState::Detached);
    EXPECT_EQ(c.first, first);
    EXPECT_EQ(c.end, end);

    ASSERT_TRUE(Run("step-forward").Ok());
    EXPECT_EQ(_ttd->PublishedCursor().frame, first + 3);
    ASSERT_TRUE(Run("step-back").Ok());
    EXPECT_EQ(_ttd->PublishedCursor().frame, first + 2);
}

TEST_P(TTDCursor_Test, ThePositionVerbAnswersTheBoundsFromTheCursor)
{
    ASSERT_TRUE(Run("start").Ok());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_TRUE(Run("stop").Ok());
    const TTDCursor c = _ttd->PublishedCursor();
    const ttd::TTDReply r = Run("position");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("session_end")->find("frame")->i, static_cast<int64_t>(c.end));
    EXPECT_EQ(r.body.find("first_frame")->i, static_cast<int64_t>(c.first));
    EXPECT_TRUE(r.body.find("history")->b);
    EXPECT_FALSE(r.body.find("paused")->b) << "not started: not paused";
    // Pentagon is the default model: 3.5 MHz / 71680 T
    EXPECT_NEAR(r.body.find("fps")->d, ttd::TTDFrameRate(_context), 1e-9);
    EXPECT_GT(r.body.find("fps")->d, 40.0);
    EXPECT_EQ(r.body.find("current")->find("frame")->i, static_cast<int64_t>(_context->emulatorState.frame_counter));
}
