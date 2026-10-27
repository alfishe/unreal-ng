// The registry actions that replace a Stream Deck scenario's REST steps (Stream Deck integration SD6, migration.md §4;
// catalogue: control surfaces actions.md §1.1): each group through ActionRegistry::Instance() on a real instance, the
// executor calling the core object the route calls. Most tests use a machine that is not started (the managers act on
// the caller's thread then); the run and machine tests start one, paused, in turbo

#include <algorithm>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/joystick/debugjoystickmanager.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/soundmanager.h"
#include "pch.h"

using namespace actions;

namespace
{
using Params = std::initializer_list<std::pair<const char*, StateNode>>;

ActionValue Press(Params params = {}, bool pressed = true)
{
    ActionValue value;
    value.pressed = pressed;
    for (const auto& [name, node] : params)
        value.params[name] = node;
    return value;
}

ActionValue Text(const std::string& text, Params params = {})
{
    ActionValue value = Press(params);
    value.kind = ValueKind::Text;
    value.text = text;
    return value;
}

ActionValue Delta(double delta, Params params = {})
{
    ActionValue value = Press(params);
    value.kind = ValueKind::Delta;
    value.delta = delta;
    return value;
}

ActionOrigin Hands()
{
    ActionOrigin origin;
    origin.origin = execution::Origin::HostDevice;
    origin.identity = "deck-1";
    origin.client = "deck-1";
    return origin;
}

std::string Testdata(const char* relative)
{
    return (TestPathHelper::FindProjectRoot() / "testdata" / relative).string();
}

/// The started machine's frame counter
uint64_t Frames(Emulator* emulator)
{
    return emulator->GetContext()->emulatorState.frame_counter;
}
}  // namespace

class HostActions_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Not started: the managers act on the caller's thread; nothing here runs the CPU
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ActionResult Run(const char* id, ActionValue value)
    {
        return ActionRegistry::Instance().Run(id, std::move(value), _id, Hands());
    }

    Emulator* _emulator = nullptr;
    std::string _id;
};

/// region <Catalogue>

TEST_F(HostActions_Test, TheCatalogueHasTheScenarioActionsWithTheirKindsParamsAndTheRoutesScopes)
{
    ActionRegistry& registry = ActionRegistry::Instance();
    for (const char* id : {"run.pause",
                           "run.resume",
                           "run.steps",
                           "run.frames",
                           "run.tstates",
                           "mach.start",
                           "mach.cold_reset",
                           "cap.screenshot_save",
                           "input.key",
                           "input.type",
                           "input.macro",
                           "input.joystick",
                           "input.release_all",
                           "basic.run",
                           "basic.inject",
                           "basic.clear",
                           "basic.mode",
                           "feature.toggle",
                           "set.speed",
                           "set.toggle",
                           "set.value",
                           "audio.mute_toggle",
                           "audio.source_mute",
                           "audio.volume_by",
                           "media.open",
                           "media.tape",
                           "media.eject",
                           "media.protect",
                           "media.save",
                           "media.create",
                           "snapshot.quick",
                           "snapshot.save",
                           "video.overscan_toggle",
                           "video.temporal",
                           "dbg.mode_toggle",
                           "dbg.breakpoints_clear",
                           "dbg.breakpoint_hits_reset",
                           "ttd.start",
                           "ttd.stop",
                           "ttd.seek",
                           "ttd.bookmark_delete",
                           "ttd.invalidate",
                           "ttd.history_limit",
                           "ttd.journal",
                           "ttd.dump",
                           "ttd.load"})
    {
        const std::optional<ActionInfo> info = registry.Find(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_FALSE(info->gui) << id << ": a core action, present headless";
    }
    EXPECT_EQ(registry.Find("input.type")->kind, ValueKind::Text);
    EXPECT_EQ(registry.Find("basic.inject")->kind, ValueKind::Text);
    EXPECT_EQ(registry.Find("audio.volume_by")->kind, ValueKind::Delta);
    EXPECT_TRUE(registry.Find("input.key")->held) << "a held key sees its release";
    EXPECT_TRUE(registry.Find("input.joystick")->held);
    for (const char* id : {"input.key", "media.open", "mach.start", "feature.toggle", "run.frames"})
        EXPECT_FALSE(registry.Find(id)->params.empty()) << id << " names its params";

    // The scope of the route each one replaces (automation_gate::CompanionScopeOf), 0 where the route changes nothing
    std::map<std::string, uint32_t> scopes;
    for (const ActionStatus& status : registry.Statuses(_id, Hands()))
        scopes[status.info.id] = status.scope;
    using namespace automation_gate;
    EXPECT_EQ(scopes["run.pause"], 0u);
    EXPECT_EQ(scopes["run.resume"], static_cast<uint32_t>(ScopeRun));
    EXPECT_EQ(scopes["run.frames"], static_cast<uint32_t>(ScopeDebug));
    EXPECT_EQ(scopes["input.key"], static_cast<uint32_t>(ScopeInput));
    EXPECT_EQ(scopes["media.open"], static_cast<uint32_t>(ScopeMedia));
    EXPECT_EQ(scopes["media.eject"], static_cast<uint32_t>(ScopeMedia));
    EXPECT_EQ(scopes["mach.start"], static_cast<uint32_t>(ScopeMachine));
    EXPECT_EQ(scopes["set.speed"], static_cast<uint32_t>(ScopeRun));
    EXPECT_EQ(scopes["video.overscan_toggle"], static_cast<uint32_t>(ScopeMachine));
    EXPECT_EQ(scopes["ttd.seek"], static_cast<uint32_t>(ScopeRewind));
    for (const char* free :
         {"audio.mute_toggle", "snapshot.save", "cap.screenshot_save", "ttd.start", "video.temporal"})
        EXPECT_EQ(scopes[free], 0u) << free << ": its route changes nothing";
}

/// endregion </Catalogue>

/// region <Input and BASIC>

TEST_F(HostActions_Test, AKeyTapsHoldsAndReleasesAndAnUnknownNameIsABadRequest)
{
    EXPECT_TRUE(Run("input.key", Press({{"key", "a"}})).ok);
    const ActionResult combo = Run("input.key", Press({{"key", "caps+1"}}));
    ASSERT_TRUE(combo.ok) << combo.message;
    EXPECT_EQ(combo.body.find("keys")->items.size(), 2u);
    const ActionResult unknown = Run("input.key", Press({{"key", "no-such-key"}}));
    EXPECT_EQ(unknown.error, "Bad Request");
    EXPECT_NE(unknown.message.find("no-such-key"), std::string::npos) << unknown.message;

    const ActionResult held = Run("input.key", Press({{"key", "a"}, {"hold", true}}));
    ASSERT_TRUE(held.ok) << held.message;
    EXPECT_TRUE(held.body.find("pressed")->b);
    const ActionResult released = Run("input.key", Press({{"key", "a"}, {"hold", true}}, false));
    ASSERT_TRUE(released.ok) << released.message;
    EXPECT_FALSE(released.body.find("pressed")->b) << "the release of a held key releases it";
    EXPECT_NE(Run("input.key", Press({{"key", "a"}}, false)).body.find("ignored"), nullptr)
        << "a tap is whole on the press";

    EXPECT_EQ(Run("input.macro", Press({{"name", "no-such-macro"}})).error, "Bad Request");
    EXPECT_TRUE(Run("input.type", Text("10")).ok);
    EXPECT_EQ(Run("input.type", Text("")).error, "Bad Request");
    EXPECT_TRUE(Run("input.release_all", Press()).ok);
}

TEST_F(HostActions_Test, TheJoystickHoldsButtonsUntilTheRelease)
{
    DebugJoystickManager* joystick = _emulator->GetContext()->pDebugManager->GetJoystickManager();
    ASSERT_NE(joystick, nullptr);
    const ActionResult pressed = Run("input.joystick", Press({{"dir", "up"}, {"fire", true}, {"hold", true}}));
    ASSERT_TRUE(pressed.ok) << pressed.message;
    const JoystickStateSnapshot down = joystick->GetState();
    EXPECT_NE(std::find(down.buttons.begin(), down.buttons.end(), "up"), down.buttons.end());
    EXPECT_NE(std::find(down.buttons.begin(), down.buttons.end(), "fire"), down.buttons.end());
    ASSERT_TRUE(Run("input.joystick", Press({{"dir", "up"}, {"fire", true}, {"hold", true}}, false)).ok);
    EXPECT_TRUE(joystick->GetState().buttons.empty()) << "released";
    EXPECT_EQ(Run("input.joystick", Press()).error, "Bad Request") << "no buttons";
}

TEST_F(HostActions_Test, BasicChecksItsModeAndClearsTheProgram)
{
    EXPECT_EQ(Run("basic.mode", Press({{"mode", "64k"}})).error, "Bad Request");
    EXPECT_TRUE(Run("basic.clear", Press()).ok);
    EXPECT_EQ(Run("basic.inject", Text("")).error, "Bad Request");
}

/// endregion </Input and BASIC>

/// region <Features, settings, audio>

TEST_F(HostActions_Test, FeaturesAndSettingsFlipOrTakeAValue)
{
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_NE(features, nullptr);
    const bool tracking = features->isEnabled(Features::kMemoryTracking);
    ASSERT_TRUE(Run("feature.toggle", Press({{"name", Features::kMemoryTracking}})).ok);
    EXPECT_NE(features->isEnabled(Features::kMemoryTracking), tracking) << "flipped";
    ASSERT_TRUE(Run("feature.toggle", Press({{"name", Features::kMemoryTracking}, {"on", tracking}})).ok);
    EXPECT_EQ(features->isEnabled(Features::kMemoryTracking), tracking) << "set";
    const ActionResult unknown = Run("feature.toggle", Press({{"name", "no-such-feature"}}));
    EXPECT_EQ(unknown.error, "Not Found");
    EXPECT_NE(unknown.body.find("available_features"), nullptr);

    const bool fastTape = features->isEnabled(Features::kFastTape);
    ASSERT_TRUE(Run("set.toggle", Press({{"name", "fast_tape"}})).ok);
    EXPECT_NE(features->isEnabled(Features::kFastTape), fastTape);
    ASSERT_TRUE(Run("set.toggle", Press({{"name", "fast_tape"}, {"on", "off"}})).ok) << "a CLI word for the bool";
    EXPECT_FALSE(features->isEnabled(Features::kFastTape));
    EXPECT_EQ(Run("set.toggle", Press({{"name", "speed"}})).error, "Not Found") << "not a bool setting";

    ASSERT_EQ(_emulator->GetSpeedMultiplier(), 1);
    // The multiplier applies at the next frame: the reply names the one asked for
    const ActionResult next = Run("set.speed", Press());
    ASSERT_TRUE(next.ok) << next.message;
    EXPECT_EQ(next.body.find("value")->i, 2) << "no value: the next speed";
    const ActionResult one = Run("set.speed", Press({{"value", "1"}}));
    ASSERT_TRUE(one.ok) << one.message;
    EXPECT_EQ(one.body.find("value")->i, 1);
    EXPECT_EQ(Run("set.speed", Press({{"value", 3}})).error, "Bad Request");

    EXPECT_TRUE(Run("set.value", Press({{"name", "ay_voicing"}, {"value", "classic"}})).ok);
    EXPECT_EQ(Run("set.value", Press({{"name", "ay_voicing"}, {"value", "no-such-voicing"}})).error, "Bad Request");
    EXPECT_EQ(Run("set.value", Press({{"name", "no_such_setting"}, {"value", true}})).error, "Not Found");
}

TEST_F(HostActions_Test, AudioMutesTheMasterAndASourceAndMovesAVolume)
{
    SoundManager* sound = _emulator->GetContext()->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_TRUE(Run("audio.mute_toggle", Press({{"on", true}})).ok);
    EXPECT_TRUE(sound->isMuted());

    const AudioDeviceInfo* beeper = sound->device(AudioSourceType::Beeper);
    ASSERT_NE(beeper, nullptr);
    const bool muted = beeper->mute;
    ASSERT_TRUE(Run("audio.source_mute", Press({{"source", "beeper"}})).ok);
    EXPECT_NE(sound->device(AudioSourceType::Beeper)->mute, muted) << "flipped";
    EXPECT_EQ(Run("audio.source_mute", Press({{"source", "master"}})).error, "Bad Request");

    sound->setDeviceVolume(AudioSourceType::Beeper, 1.0f);
    const ActionResult down = Run("audio.volume_by", Delta(-2, {{"source", "beeper"}, {"step", 0.1}}));
    ASSERT_TRUE(down.ok) << down.message;
    EXPECT_NEAR(sound->device(AudioSourceType::Beeper)->volume, 0.8f, 1e-4);
    ASSERT_TRUE(Run("audio.volume_by", Delta(50, {{"source", "beeper"}})).ok);
    EXPECT_FLOAT_EQ(sound->device(AudioSourceType::Beeper)->volume, 1.0f) << "clamped at 1";
}

/// endregion </Features, settings, audio>

/// region <Media and snapshots>

TEST_F(HostActions_Test, ATapeOpensWithoutAutostartAndTheDeckObeysItsOps)
{
    const ActionResult opened =
        Run("media.open", Press({{"path", Testdata("loaders/tap/AYtest_v0.2.tap")}, {"autostart", false}}));
    ASSERT_TRUE(opened.ok) << opened.message;
    EXPECT_EQ(opened.body.find("kind")->s, "tape");
    EXPECT_TRUE(Run("media.tape", Press({{"op", "rewind"}})).ok);
    const ActionResult seek = Run("media.tape", Press({{"op", "seek"}, {"block", 1}}));
    EXPECT_TRUE(seek.ok) << seek.message;
    EXPECT_TRUE(Run("media.tape", Press({{"op", "stop"}})).ok);
    EXPECT_EQ(Run("media.tape", Press({{"op", "fast-forward"}})).error, "Bad Request");
    EXPECT_TRUE(Run("media.eject", Press({{"drive", "tape"}})).ok);

    EXPECT_EQ(Run("media.open", Press({{"path", Testdata("loaders/tap/no-such-file.tap")}})).error, "Not Found");
    EXPECT_EQ(Run("media.open", Press()).error, "Bad Request");
}

TEST_F(HostActions_Test, ADiskOpensInADriveTheProtectFlipsAndTheEjectDiscards)
{
    const ActionResult opened =
        Run("media.open", Press({{"path", Testdata("loaders/trd/EyeAche.trd")}, {"autostart", false}}));
    ASSERT_TRUE(opened.ok) << opened.message;
    EXPECT_EQ(opened.body.find("kind")->s, "disk");
    EXPECT_EQ(opened.body.find("drive")->s, "A");

    MediaManager* media = _emulator->GetContext()->pMediaManager;
    ASSERT_NE(media, nullptr);
    const ActionResult protect = Run("media.protect", Press({{"drive", "A"}}));
    ASSERT_TRUE(protect.ok) << protect.message;
    const std::string slot = protect.body.find("slot")->s;
    const bool first = media->Info(slot)->writeProtect;
    ASSERT_TRUE(Run("media.protect", Press({{"drive", "A"}})).ok);
    EXPECT_NE(media->Info(slot)->writeProtect, first) << "flipped";

    const ActionResult ejected = Run("media.eject", Press({{"drive", "A"}, {"discard", true}}));
    EXPECT_TRUE(ejected.ok) << ejected.message;
    EXPECT_EQ(Run("media.eject", Press()).error, "Bad Request") << "no drive";
}

TEST_F(HostActions_Test, SnapshotsSaveRefuseAnOverwriteAndTheQuickSlotsRoundTrip)
{
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath("host-actions-snapshots");
    ASSERT_TRUE(FileHelper::CreateFolders(folder));
    const std::string path = FileHelper::PathCombine(folder, "saved.szx");
    const ActionResult saved = Run("snapshot.save", Press({{"path", path}}));
    ASSERT_TRUE(saved.ok) << saved.message;
    EXPECT_TRUE(FileHelper::FileExists(path));
    EXPECT_EQ(Run("snapshot.save", Press({{"path", path}})).error, "Conflict") << "an existing file needs force";
    EXPECT_TRUE(Run("snapshot.save", Press({{"path", path}, {"force", true}})).ok);

    const ActionResult opened = Run("media.open", Press({{"path", path}}));
    ASSERT_TRUE(opened.ok) << opened.message;
    EXPECT_EQ(opened.body.find("kind")->s, "snapshot");

    const ActionResult quick = Run("snapshot.quick", Press({{"op", "save"}, {"slot", 2}, {"dir", folder}}));
    ASSERT_TRUE(quick.ok) << quick.message;
    EXPECT_TRUE(FileHelper::FileExists(FileHelper::PathCombine(folder, "quick-2.szx")));
    const ActionResult back = Run("snapshot.quick", Press({{"op", "load"}, {"slot", 2}, {"dir", folder}}));
    ASSERT_TRUE(back.ok) << back.message;
    EXPECT_FALSE(back.body.find("model_switched")->b) << "the same model";
    EXPECT_EQ(Run("snapshot.quick", Press({{"op", "load"}, {"slot", 3}, {"dir", folder}})).error, "Conflict")
        << "an empty slot";
    EXPECT_EQ(Run("snapshot.quick", Press({{"op", "save"}, {"slot", 10}, {"dir", folder}})).error, "Bad Request");
}

/// endregion </Media and snapshots>

/// region <Video, debugger, capture, time travel>

TEST_F(HostActions_Test, VideoAndDebuggerTogglesAndTheirErrors)
{
    const bool overscan = _emulator->IsOverscanMode();
    const ActionResult flipped = Run("video.overscan_toggle", Press());
    ASSERT_TRUE(flipped.ok) << flipped.message;
    EXPECT_NE(_emulator->IsOverscanMode(), overscan) << "the Pentagon has an overscan mode";
    ASSERT_TRUE(Run("video.overscan_toggle", Press({{"on", overscan}})).ok);
    EXPECT_EQ(_emulator->IsOverscanMode(), overscan);
    EXPECT_EQ(Run("video.temporal", Press({{"algorithm", "no-such-algorithm"}})).error, "Bad Request");
    EXPECT_TRUE(Run("video.temporal", Press({{"algorithm", "off"}})).ok);

    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_TRUE(Run("dbg.mode_toggle", Press({{"on", true}})).ok);
    EXPECT_TRUE(features->isEnabled(Features::kDebugMode));
    ASSERT_TRUE(Run("dbg.mode_toggle", Press()).ok);
    EXPECT_FALSE(features->isEnabled(Features::kDebugMode)) << "flipped back";
    EXPECT_TRUE(Run("dbg.breakpoints_clear", Press()).ok);
    EXPECT_EQ(Run("dbg.breakpoint_hits_reset", Press({{"id", 999}})).error, "Not Found");
    EXPECT_TRUE(Run("dbg.breakpoint_hits_reset", Press()).ok);

    EXPECT_EQ(Run("cap.screenshot_save", Press()).error, "Bad Request") << "no path";
    EXPECT_EQ(Run("cap.screenshot_save", Press({{"path", "x.png"}, {"format", "no-such-format"}})).error,
              "Bad Request");
}

TEST_F(HostActions_Test, TimeTravelVerbsTakeTheirParamsAsOptions)
{
    // Without a recorded history the verbs that act on one are not available yet (a deck greys them out)
    for (const char* id : {"ttd.seek", "ttd.bookmark_delete", "ttd.invalidate", "ttd.dump"})
    {
        const ActionResult early = Run(id, Press());
        EXPECT_EQ(early.error, "not_now") << id;
        EXPECT_EQ(early.message, "record first") << id;
    }
    ASSERT_TRUE(Run("ttd.start", Press()).ok);
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_TRUE(Run("ttd.stop", Press()).ok);

    // The params reach the controller as its options: a missing label and an unknown one are its own answers
    EXPECT_EQ(Run("ttd.bookmark_delete", Press()).error, "Bad Request") << "no label";
    const ActionResult missing = Run("ttd.bookmark_delete", Press({{"label", "no-such-bookmark"}}));
    EXPECT_EQ(missing.error, "Not Found") << missing.message;
    EXPECT_NE(missing.message.find("no-such-bookmark"), std::string::npos) << missing.message;
    EXPECT_EQ(Run("ttd.seek", Press({{"frame", "not-a-frame"}})).error, "Bad Request");
}

/// endregion </Video, debugger, capture, time travel>

/// region <Run and machine (started machines)>

/// Counted runs on a paused machine, refused while it runs; BASIC typing needs a running machine. Boot-bound (a
/// started machine paused): about 30 ms
TEST_F(HostActions_Test, CountedRunsNeedAPauseAndTypingNeedsARunningMachine)
{
    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return Frames(_emulator) > 1; }));
    ASSERT_TRUE(Run("run.pause", Press()).ok);
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsPaused(); }));

    const uint64_t before = Frames(_emulator);
    const ActionResult frames = Run("run.frames", Press({{"count", 3}}));
    ASSERT_TRUE(frames.ok) << frames.message;
    EXPECT_EQ(Frames(_emulator) - before, 3u);
    const ActionResult steps = Run("run.steps", Press({{"count", "5"}}));
    ASSERT_TRUE(steps.ok) << steps.message;
    EXPECT_EQ(steps.body.find("executed")->i, 5);
    EXPECT_TRUE(Run("run.tstates", Press({{"count", 100}})).ok);
    EXPECT_EQ(Run("run.frames", Press({{"count", "many"}})).error, "Bad Request");
    EXPECT_EQ(Run("basic.run", Press()).error, "not_now") << "typing needs frames to run";

    ASSERT_TRUE(Run("run.resume", Press()).ok);
    ASSERT_TRUE(TestWait::For([&] { return !_emulator->IsPaused(); }));
    EXPECT_EQ(Run("run.frames", Press()).error, "not_now") << "needs a pause";
}

/// mach.start replaces the instance in place (symbolic id kept, the old one gone), mach.cold_reset restarts the same
/// model. Boot-bound (two machines created and started): about 40 ms
TEST_F(HostActions_Test, AStartReplacesTheInstanceAndAColdResetRestartsTheModel)
{
    EmulatorManager& manager = *EmulatorManager::GetInstance();
    const std::string symbolic = _emulator->GetSymbolicId();
    EXPECT_EQ(Run("mach.start", Press({{"model", "NO-SUCH-MODEL"}})).error, "Bad Request");
    EXPECT_EQ(Run("mach.start", Press()).error, "Bad Request") << "no model";

    const ActionResult started = Run("mach.start", Press({{"model", "48K"}}));
    ASSERT_TRUE(started.ok) << started.message;
    const std::string replaced = started.body.find("emulator_id")->s;
    EXPECT_NE(replaced, _id);
    EXPECT_EQ(manager.GetEmulator(_id), nullptr) << "the old instance is gone";
    _emulator = nullptr;  // removed by the switch
    std::shared_ptr<Emulator> current = manager.GetEmulator(replaced);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->GetSymbolicId(), symbolic) << "the symbolic id is kept";

    _id = replaced;
    const ActionResult restarted = Run("mach.cold_reset", Press({{"ram_power_on", "zero"}}));
    ASSERT_TRUE(restarted.ok) << restarted.message;
    EXPECT_EQ(restarted.body.find("model")->s, "48K");
    const std::string after = restarted.body.find("emulator_id")->s;
    EXPECT_NE(after, replaced);
    EXPECT_EQ(manager.GetEmulator(replaced), nullptr);
    current.reset();
    manager.RemoveEmulator(after);
    EXPECT_EQ(Run("run.pause", Press()).error, "Not Found") << "the instance is gone";
}

/// mach.cold_reset keeps the run state the user had: a paused machine comes back started and paused, so resume and
/// typing work on it (not "initialized", where both are refused). Boot-bound (two started machines): about 40 ms
TEST_F(HostActions_Test, AColdResetOfAPausedMachineLeavesItStartedAndPaused)
{
    EmulatorManager& manager = *EmulatorManager::GetInstance();
    _emulator->StartAsync();
    _emulator->Pause();
    ASSERT_TRUE(_emulator->IsRunning() && _emulator->IsPaused());

    const ActionResult restarted = Run("mach.cold_reset", Press());
    ASSERT_TRUE(restarted.ok) << restarted.message;
    _emulator = nullptr;  // removed by the switch
    const std::string after = restarted.body.find("emulator_id")->s;
    std::shared_ptr<Emulator> current = manager.GetEmulator(after);
    ASSERT_NE(current, nullptr);
    EXPECT_TRUE(current->IsRunning()) << "started";
    EXPECT_TRUE(current->IsPaused()) << "and paused, as the old one was";
    current.reset();
    _id = after;
    EXPECT_TRUE(Run("run.resume", Press()).ok) << "a started machine resumes";
    manager.RemoveEmulator(after);
}

/// endregion </Run and machine>
