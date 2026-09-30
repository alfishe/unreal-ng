// RzxLauncher: the entry point every surface plays through - a recording made
// on another model switches the model first (media kept) and plays on the new
// machine; with switching off the mismatch is reported; the option and status
// text forms.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/rzx/rzxlauncher.h"

using namespace rzx;

namespace
{
    std::string Fixture(const std::string& name)
    {
        return (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "rzx" / name).string();
    }

    class RzxLauncher_Test : public ::testing::Test
    {
    protected:
        std::string _emulatorId;

        void SetUp() override
        {
            auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("rzx-launch", "48K",
                                                                                    LoggerLevel::LogError);
            ASSERT_NE(emulator, nullptr);
            _emulatorId = emulator->GetId();
        }

        void TearDown() override
        {
            EmulatorManager::GetInstance()->RemoveEmulator(_emulatorId);
        }
    };
}  // namespace

/// A 128K recording on a 48K: the model switches, the new machine plays
TEST_F(RzxLauncher_Test, SwitchesTheModelForTheRecording)
{
    LaunchRequest request;
    request.emulatorId = _emulatorId;
    request.path = Fixture("archive/greenberet.rzx");
    const LaunchResult result = RzxLauncher::Play(request);
    ASSERT_TRUE(result.play.Ok()) << result.play.message;
    ASSERT_NE(result.emulator, nullptr);
    EXPECT_TRUE(result.modelSwitched);
    EXPECT_EQ(result.previousEmulatorId, _emulatorId);
    EXPECT_EQ(result.switchedToModel, "128k");
    EXPECT_NE(result.emulator->GetId(), _emulatorId);
    EXPECT_EQ(result.emulator->GetContext()->config.mem_model, MM_SPECTRUM128);
    EXPECT_TRUE(result.emulator->IsRzxPlaying());
    EXPECT_EQ(EmulatorManager::GetInstance()->GetEmulator(_emulatorId), nullptr) << "the old machine is gone";

    result.emulator->StopRzx();
    result.emulator->Stop();
    _emulatorId = result.emulator->GetId();
}

TEST_F(RzxLauncher_Test, WithoutSwitchingTheMismatchIsReported)
{
    LaunchRequest request;
    request.emulatorId = _emulatorId;
    request.path = Fixture("archive/greenberet.rzx");
    request.switchModel = false;
    const LaunchResult result = RzxLauncher::Play(request);
    EXPECT_EQ(result.play.error, PlayError::ModelMismatch);
    EXPECT_EQ(result.play.requiredModel, "128k");
    EXPECT_FALSE(result.modelSwitched);
    EXPECT_EQ(result.emulator->GetId(), _emulatorId);
}

TEST_F(RzxLauncher_Test, TextForms)
{
    DesyncMode mode = DesyncMode::Strict;
    EXPECT_TRUE(RzxLauncher::ParseDesyncMode("tolerant", mode));
    EXPECT_EQ(mode, DesyncMode::Tolerant);
    EXPECT_FALSE(RzxLauncher::ParseDesyncMode("loose", mode));
    EXPECT_STREQ(RzxLauncher::DesyncModeName(DesyncMode::Strict), "strict");

    SessionStatus status;
    EXPECT_EQ(RzxLauncher::StatusLine(status), "no RZX recording played");
    status.loaded = true;
    status.player.state = PlayerState::Playing;
    status.player.frame = 1200;
    status.player.totalFrames = 32315;
    status.player.blocks = 1;
    EXPECT_EQ(RzxLauncher::StatusLine(status), "playing frame 1200 / 32315 (3.7%), block 1 / 1, 0 desyncs");
}
