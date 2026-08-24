#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/video/screen.h"

/// Video-mode-change contract tests.
///
/// Mode switches come from two sources: the UI (Pentagon overscan toggle) and
/// GUEST SOFTWARE writing mode ports (Pentagon AlCo via EFF7, Profi via DFFD,
/// ATM via FF77). Both reallocate the framebuffer/present buffer when the
/// geometry changes, and both must:
///   1. survive InitRaster's per-frame re-detection (forced overscan flag),
///   2. produce coherent buffer geometry (present buffer follows framebuffer),
///   3. keep the buffer ADDRESS on same-size switches (consumers hold raw
///      pointers without locks - reallocation would be a use-after-free),
///   4. post NC_VIDEO_MODE_CHANGED so GUI consumers re-attach.

/// region <Test helpers>

/// NC_VIDEO_MODE_CHANGED observer used by the notification test.
///
/// Must subscribe as an Observer instance + method pair: RemoveObserver
/// matches those by the stable instance pointer. The std::function overload
/// compares the heap address of the callable wrapper, which is different in
/// every std::function copy - RemoveObserver(topic, lambda) silently matches
/// NOTHING for capturing lambdas, the observer leaks forever and later fires
/// on dead stack locals from the MessageCenter thread, corrupting whichever
/// test reuses that stack region (first seen as a segfault in
/// ZXEvoBoot_Test when the BaseConf boot posts video-mode changes).
class ModeChangeObserver : public Observer
{
public:
    std::atomic<int> received{0};
    std::atomic<bool> idMatches{false};
    unreal::UUID expectedId{};

    void OnModeChanged(int id, Message* message)
    {
        (void)id;
        if (message && message->obj)
        {
            auto* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
            if (payload && payload->_emulatorId == expectedId)
                idMatches.store(true);
        }
        received.fetch_add(1);
    }
};

/// endregion </Test helpers>

class VideoModeChange_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _screen = _context->pScreen;
        ASSERT_NE(_screen, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(VideoModeChange_Test, ForcedOverscan_PersistsAcrossRedetection)
{
    // The UI toggle is a user override, not guest-visible port state.
    // InitRaster re-detects the mode from config/ports every frame and must
    // honor the override instead of reverting to the model's base mode
    // (the regression that broke the merged pentagon-overscan feature).
    ASSERT_EQ(_screen->GetVideoMode(), M_PENTAGON128K);

    ASSERT_TRUE(_emulator->SetOverscanMode(true));
    EXPECT_EQ(_screen->GetVideoMode(), M_P384);

    for (int frame = 0; frame < 3; frame++)
    {
        _screen->InitFrame();
        EXPECT_EQ(_screen->GetVideoMode(), M_P384)
            << "Re-detection reverted forced overscan on frame " << frame;
    }

    ASSERT_TRUE(_emulator->SetOverscanMode(false));
    _screen->InitFrame();
    EXPECT_EQ(_screen->GetVideoMode(), M_PENTAGON128K);
}

TEST_F(VideoModeChange_Test, SizeChangingSwitch_BuffersStayCoherent)
{
    // A size-changing switch (352x288 -> 384x304) must resize the present
    // buffer together with the framebuffer, serve new-geometry consumers, and
    // REJECT stale-geometry consumers (never feed a torn/partial frame) -
    // rejection is the failure NC_VIDEO_MODE_CHANGED recovers from.
    //
    // KNOWN GAP (documented, not fixed here): guest-programmed AlCo modes
    // (EFF7 writes) are detected by InitRaster but every AlCo/ATM branch
    // early-returns BEFORE the SetVideoMode application step, so guest mode
    // switches have never taken effect. Wiring them live would activate
    // untested renderers (M_PMC/M_P16/M_PHR) as a side effect - out of scope.
    ASSERT_TRUE(_emulator->SetOverscanMode(true));

    auto& fb = _screen->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 384u);
    EXPECT_EQ(fb.height, 304u);

    const size_t newSize = 384 * 304 * 4;
    std::vector<uint8_t> dstNew(newSize);
    EXPECT_TRUE(_screen->CopyPresentedFramebuffer(dstNew.data(), dstNew.size()))
        << "Present buffer must be resized together with the framebuffer";

    std::vector<uint8_t> dstOld(352 * 288 * 4);
    EXPECT_FALSE(_screen->CopyPresentedFramebuffer(dstOld.data(), dstOld.size()))
        << "Stale-geometry consumers must be rejected (re-attach required)";

    ASSERT_TRUE(_emulator->SetOverscanMode(false));
    auto& fbBack = _screen->GetFramebufferDescriptor();
    EXPECT_EQ(fbBack.width, 352u);
    EXPECT_EQ(fbBack.height, 288u);

    std::vector<uint8_t> dstStd(352 * 288 * 4);
    EXPECT_TRUE(_screen->CopyPresentedFramebuffer(dstStd.data(), dstStd.size()));
}

TEST_F(VideoModeChange_Test, SameSizeSwitch_KeepsFramebufferAddress)
{
    // M_PENTAGON128K and M_ZX48 share 352x288: consumers hold raw pointers
    // (DeviceScreen live QImage wrap, videowall tiles), so an identical-size
    // switch must NOT reallocate - that frees memory mid-read for zero upside.
    auto& fb = _screen->GetFramebufferDescriptor();
    uint8_t* before = fb.memoryBuffer;
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(fb.memoryBufferSize, (size_t)(352 * 288 * 4));

    _screen->SetVideoMode(M_ZX48);
    EXPECT_EQ(_screen->GetFramebufferDescriptor().memoryBuffer, before)
        << "Same-size mode switch must keep the framebuffer address";

    _screen->SetVideoMode(M_PENTAGON128K);
    EXPECT_EQ(_screen->GetFramebufferDescriptor().memoryBuffer, before);
}

TEST_F(VideoModeChange_Test, PresentQueue_DelaysVideoByConfiguredFrames)
{
    // A/V sync: video presentation trails the newest latched frame by the
    // configured delay so it lands at the same constant latency as the audio
    // path (ring + HW buffer ~= 2 frames). During queue fill the delay is
    // clamped to what exists (the 1-2 frame startup buffering).
    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_NE(fb.memoryBuffer, nullptr);

    std::vector<uint8_t> dst(fb.memoryBufferSize);
    auto latchMarked = [&](uint8_t marker) {
        fb.memoryBuffer[0] = marker;
        _screen->LatchFramebuffer();
    };

    _screen->SetPresentDelayFrames(2);
    ASSERT_EQ(_screen->GetPresentDelayFrames(), 2);

    // Queue fill: with only one frame latched, present it (clamped delay)
    latchMarked(10);
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(dst.data(), dst.size()));
    EXPECT_EQ(dst[0], 10) << "Startup: delay clamps to the oldest available frame";

    // Steady state: present newest-2
    latchMarked(20);
    latchMarked(30);
    latchMarked(40);
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(dst.data(), dst.size()));
    EXPECT_EQ(dst[0], 20) << "Delay 2 must present the frame latched 2 frames ago";

    // Low-latency mode: newest frame immediately
    _screen->SetPresentDelayFrames(0);
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(dst.data(), dst.size()));
    EXPECT_EQ(dst[0], 40) << "Delay 0 must present the newest latched frame";

    // Loaded config defaults to auto (2 frames) - the value AllocateFramebuffer
    // re-applies on every mode switch
    EXPECT_EQ(_context->config.videoPresentDelayFrames, -1) << "Loaded config must default to auto";
    _screen->SetVideoMode(M_ZX48);
    _screen->SetVideoMode(M_PENTAGON128K);
    EXPECT_EQ(_screen->GetPresentDelayFrames(), 2) << "auto = 2 frames after reallocation";
}

TEST_F(VideoModeChange_Test, ModeChange_PostsNotificationWithEmulatorId)
{
    // GUI consumers re-attach on NC_VIDEO_MODE_CHANGED; the payload must
    // carry the emulator ID so multi-emulator listeners can filter.
    ModeChangeObserver observer;
    observer.expectedId = _emulator->GetUUID();

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(&observer);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&ModeChangeObserver::OnModeChanged);
    messageCenter.AddObserver(NC_VIDEO_MODE_CHANGED, observerInstance, callback);

    ASSERT_TRUE(_emulator->SetOverscanMode(true));

    // Notification dispatch is asynchronous (MessageCenter thread)
    auto start = std::chrono::steady_clock::now();
    while (observer.received.load() == 0 &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Waited for the first dispatch to complete above, so no in-flight
    // dispatch can reference the observer after this removal
    messageCenter.RemoveObserver(NC_VIDEO_MODE_CHANGED, observerInstance, callback);

    EXPECT_GE(observer.received.load(), 1) << "SetVideoMode must post NC_VIDEO_MODE_CHANGED";
    EXPECT_TRUE(observer.idMatches.load()) << "Payload must carry the posting emulator's ID";

    _emulator->SetOverscanMode(false);
}
