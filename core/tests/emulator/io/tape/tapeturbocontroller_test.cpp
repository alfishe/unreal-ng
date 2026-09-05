#include "tapeturbocontroller_test.h"

#include <fstream>
#include <stdexcept>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"

/// region <SetUp / TearDown>

void TapeTurboController_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    if (!_emulator->Init())
    {
        throw std::runtime_error("Failed to initialize emulator for TapeTurboController_Test");
    }

    _context = _emulator->GetContext();

    _tape = new TapeCUT(_context);
    _controller = new TapeTurboControllerCUT(_context, *_tape);

    // Hermetic feature state: never depend on a features.ini sitting in the
    // working directory (the toggle rows set/reset it explicitly themselves)
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kTurboTape, true));

    // No turbo may leak between tests through the fresh-but-paranoid back door
    ASSERT_FALSE(_context->pCore->IsTurboMode());
}

void TapeTurboController_Test::TearDown()
{
    // Controller first: its destructor releases a warp it still owns while the
    // Core (owned by _emulator) is still alive to receive the disable
    if (_controller != nullptr)
    {
        delete _controller;
        _controller = nullptr;
    }

    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    if (_emulator != nullptr)
    {
        _emulator->Stop();
        _emulator->Release();
        delete _emulator;
        _emulator = nullptr;
    }

    _context = nullptr;  // Owned by _emulator, don't delete
}

/// endregion </SetUp / TearDown>

/// region <Helpers>

std::vector<uint8_t> TapeTurboController_Test::MakeTAPBlock(uint8_t flag, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> block;
    block.reserve(payload.size() + 2);

    uint8_t checksum = flag;
    block.push_back(flag);
    for (const uint8_t byte : payload)
    {
        block.push_back(byte);
        checksum ^= byte;
    }
    block.push_back(checksum);

    return block;
}

std::string TapeTurboController_Test::WriteTAPFile(const std::string& name, const std::vector<std::vector<uint8_t>>& blocks)
{
    const std::string path = TestPathHelper::GetTestScratchPath("tapeturbo/" + name);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const std::vector<uint8_t>& block : blocks)
    {
        const uint16_t length = static_cast<uint16_t>(block.size());
        out.write(reinterpret_cast<const char*>(&length), sizeof length);
        out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    }

    return path;
}

void TapeTurboController_Test::MountSingleBlockTape(const std::string& name)
{
    // 4 KiB of data plays for ~1200 frames, and a single block means every
    // stopPlayback() during playback lands on the natural end-of-tape state
    const std::vector<uint8_t> payload(4096, 0xA5);
    _context->coreState.tapeFilePath = WriteTAPFile(name, { MakeTAPBlock(0xFF, payload) });

    ASSERT_TRUE(_tape->EnsureImageLoaded());
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);
}

void TapeTurboController_Test::StartPlayback()
{
    _tape->StartPlaybackAtCursor();
    _tape->handleFrameStart();  // Bind the in-flight block like MainLoop does
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);
}

/// endregion </Helpers>

/// region <E1: engage>

// E1: signal playback running + feature on + nobody owns turbo -> engage the
// machine's warp. The only transition that ever turns turbo ON.
TEST_F(TapeTurboController_Test, EngagesWhilePlaying)
{
    MountSingleBlockTape("e1-engage.tap");
    StartPlayback();

    _controller->handleFrameEnd();

    EXPECT_TRUE(_context->pCore->IsTurboMode());
    EXPECT_TRUE(_controller->IsAutoTurboActive());
}

// Precondition rows: playback not running means no warp, ever — Idle (fresh
// mount), Paused (watchdog freeze) and Ended (end-of-tape) all stay at 50 Hz
TEST_F(TapeTurboController_Test, NoEngageWhileIdle)
{
    MountSingleBlockTape("e1-idle.tap");

    _controller->handleFrameEnd();

    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());
}

TEST_F(TapeTurboController_Test, NoEngageWhilePausedOrEnded)
{
    MountSingleBlockTape("e1-not-playing.tap");

    // Paused: the read-gap watchdog's freeze
    StartPlayback();
    _tape->pausePlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());

    // Ended: the natural end-of-tape stop
    _tape->ResumePlaybackFromPause();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);
    _tape->stopPlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());
}

// E1 with the feature off: the lazy gate must hold on the engage side too
TEST_F(TapeTurboController_Test, NoEngageWhenFeatureOff)
{
    MountSingleBlockTape("e1-feature-off.tap");
    StartPlayback();
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kTurboTape, false));

    _controller->handleFrameEnd();

    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());
}

/// endregion </E1: engage>

/// region <E2/E3: disengage on playback over>

// E2: the watchdog freeze (Paused) ends the warp session; a poll-driven resume
// (Playing again) starts a fresh one
TEST_F(TapeTurboController_Test, DisengageOnPauseAndReengageOnResume)
{
    MountSingleBlockTape("e2-pause.tap");
    StartPlayback();

    _controller->handleFrameEnd();
    ASSERT_TRUE(_context->pCore->IsTurboMode());

    // Watchdog freeze: playback over (the loader stopped listening)
    _tape->pausePlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());

    // Sustained EAR polling resumes the deck: warp comes back (E1 again)
    _tape->ResumePlaybackFromPause();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);
    _controller->handleFrameEnd();
    EXPECT_TRUE(_context->pCore->IsTurboMode());
    EXPECT_TRUE(_controller->IsAutoTurboActive());
}

// E3: the natural end-of-tape stop stands the warp down
TEST_F(TapeTurboController_Test, DisengageOnEndOfTape)
{
    MountSingleBlockTape("e3-ended.tap");
    StartPlayback();

    _controller->handleFrameEnd();
    ASSERT_TRUE(_context->pCore->IsTurboMode());

    _tape->stopPlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
    _controller->handleFrameEnd();

    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());
}

/// endregion </E2/E3: disengage on playback over>

/// region <E4: feature toggle>

// E4: turning the feature off mid-warp disables the turbo we own and the
// lazy gate keeps it off while playback continues; re-enaging works after the
// feature comes back
TEST_F(TapeTurboController_Test, FeatureToggleOffDisengagesAndHolds)
{
    MountSingleBlockTape("e4-toggle.tap");
    StartPlayback();

    _controller->handleFrameEnd();
    ASSERT_TRUE(_context->pCore->IsTurboMode());

    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kTurboTape, false));
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());

    // Still playing, still no feature: no re-engage on further ticks
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());

    // Feature back on: warp resumes without any playback transition
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kTurboTape, true));
    _controller->handleFrameEnd();
    EXPECT_TRUE(_context->pCore->IsTurboMode());
}

/// endregion </E4: feature toggle>

/// region <E5/E6: ownership>

// E5: a turbo the controller does not own (the user's manual toggle) is never
// engaged on top of and never disabled — not on playback over, not ever
TEST_F(TapeTurboController_Test, ManualTurboIsNeverTouched)
{
    MountSingleBlockTape("e5-manual.tap");

    // The user's warp first, playback second: E1's ownership check stands down
    _context->pCore->EnableTurboMode();
    StartPlayback();
    ASSERT_TRUE(_context->pCore->IsTurboMode());

    _controller->handleFrameEnd();
    EXPECT_TRUE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());

    // Playback over: the manual warp must survive our disengage transitions
    _tape->stopPlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
    _controller->handleFrameEnd();
    EXPECT_TRUE(_context->pCore->IsTurboMode());
}

// E6: switching off our warp mid-playback is an explicit "no warp" vote for
// the rest of the playback session; a fresh playback session starts clean
TEST_F(TapeTurboController_Test, ManualDisableDuringPlaybackSuppressesUntilPlaybackOver)
{
    MountSingleBlockTape("e6-suppress.tap");
    StartPlayback();

    _controller->handleFrameEnd();
    ASSERT_TRUE(_context->pCore->IsTurboMode());
    ASSERT_FALSE(_controller->Suppressed());

    // The user disables our warp while the tape keeps rolling
    _context->pCore->DisableTurboMode();
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsAutoTurboActive());
    EXPECT_TRUE(_controller->Suppressed());

    // Suppression holds while playback continues (no re-engage, no fighting
    // the user's decision)
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);
    _controller->handleFrameEnd();
    EXPECT_FALSE(_context->pCore->IsTurboMode());

    // Playback over: the veto is lifted with the session
    _tape->stopPlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
    _controller->handleFrameEnd();
    EXPECT_FALSE(_controller->Suppressed());

    // A fresh session engages again like the very first one
    _tape->RewindToStart();
    StartPlayback();
    _controller->handleFrameEnd();
    EXPECT_TRUE(_context->pCore->IsTurboMode());
    EXPECT_TRUE(_controller->IsAutoTurboActive());
}

/// endregion </E5/E6: ownership>

/// region <Lifecycle>

// The destructor must never leave an owned warp behind a still-alive Core
// (destruction order vs. Core is not guaranteed across embedders)
TEST_F(TapeTurboController_Test, DestructorReleasesOwnedTurbo)
{
    MountSingleBlockTape("dtor-release.tap");
    StartPlayback();

    _controller->handleFrameEnd();
    ASSERT_TRUE(_context->pCore->IsTurboMode());

    delete _controller;
    _controller = nullptr;

    EXPECT_FALSE(_context->pCore->IsTurboMode());
}

/// endregion </Lifecycle>
