// The machine area's catalogue actions (hostactions_machine.cpp) and the core pieces under them: machine names
// (machines::Resolve, the machine history), the recent files, the recording presets and control, the blank TR-DOS disk.
// Most tests use a machine that is not started (the managers act on the caller's thread); the machine switches and
// the TTD clip start one

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "common/filehelper.h"
#include "emulator/actions/actionregistry.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/actions/readouts.h"
#include "emulator/capture/recordingcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/machinenames.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/recentfiles.h"
#include "pch.h"
#include "unrealdisk/trdosfs.h"

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

std::string Scratch(const char* leaf)
{
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath(leaf);
    FileHelper::CreateFolders(folder);
    return folder;
}

const StateNode* Field(const StateNode& node, std::initializer_list<const char*> path)
{
    const StateNode* at = &node;
    for (const char* name : path)
    {
        at = at != nullptr ? at->find(name) : nullptr;
    }
    return at;
}
}  // namespace

class HostActionsMachine_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
        RecentFiles::Instance().Clear();
    }

    void TearDown() override
    {
        RecentFiles::Instance().Clear();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        for (const std::string& id : _extra)
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    ActionResult Run(const char* id, ActionValue value, const std::string& instance = {})
    {
        return ActionRegistry::Instance().Run(id, std::move(value), instance.empty() ? _id : instance, Hands());
    }

    Emulator* _emulator = nullptr;
    std::string _id;
    std::vector<std::string> _extra;  ///< instances a test made (removed at the end)
};

/// region <Catalogue>

TEST_F(HostActionsMachine_Test, TheAreaActionsAreInTheCatalogueWithParamsScopesAndToggles)
{
    ActionRegistry& registry = ActionRegistry::Instance();
    for (const char* id : {"mach.switch", "mach.previous", "media.insert", "media.swap", "media.export",
                           "media.discard", "media.rescan", "media.open_recent", "rzx.play", "rzx.stop", "rzx.seek",
                           "input.joystick_type", "input.mouse_button", "input.mouse_release", "cap.record_toggle",
                           "cap.record_pause", "cap.clip_last", "cap.audio_capture", "ttd.export_clip"})
    {
        const std::optional<ActionInfo> info = registry.Find(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_FALSE(info->gui) << id;
        EXPECT_FALSE(info->description.empty()) << id << " has a description";
    }
    EXPECT_TRUE(registry.Find("mach.switch")->toggle);
    EXPECT_TRUE(registry.Find("cap.record_toggle")->toggle);
    EXPECT_TRUE(registry.Find("cap.record_pause")->toggle);
    EXPECT_TRUE(registry.Find("input.mouse_button")->held);
    EXPECT_EQ(registry.Find("cap.record_toggle")->description.find("not available"), std::string::npos)
        << "no longer a stub";

    std::map<std::string, ActionStatus> statuses;
    for (const ActionStatus& status : registry.Statuses(_id, Hands()))
        statuses[status.info.id] = status;
    using namespace automation_gate;
    EXPECT_EQ(statuses["mach.switch"].scope, static_cast<uint32_t>(ScopeMachine));
    EXPECT_EQ(statuses["media.export"].scope, static_cast<uint32_t>(ScopeMedia));
    EXPECT_EQ(statuses["rzx.play"].scope, static_cast<uint32_t>(ScopeMedia));
    EXPECT_EQ(statuses["input.joystick_type"].scope, static_cast<uint32_t>(ScopeMachine)) << "a plug: hardware";
    EXPECT_NE(statuses["cap.record_toggle"].error, "Not Implemented");
    EXPECT_TRUE(statuses["cap.record_toggle"].available) << statuses["cap.record_toggle"].reason;
    EXPECT_FALSE(statuses["cap.clip_last"].available) << "no TTD recording";
    EXPECT_FALSE(statuses["rzx.seek"].available) << "no recording played";

    // The area's readouts are in the vocabulary
    for (const char* name :
         {"machine.previous", "machine.switches", "media.recent", "media.rzx", "input.connectors", "cap.record"})
    {
        const ReadoutSet set = ReadReadouts(_id, {name});
        EXPECT_TRUE(set.unknown.empty()) << name;
        EXPECT_NE(set.values.find(name), nullptr) << name;
    }
}

/// endregion </Catalogue>

/// region <Machine names and history>

TEST(MachineNames_Test, ModelsVariantsAndZxPolyConfigurationsResolveAsTheManagerCreatesThem)
{
    machines::ResolvedName out;
    std::string error;
    ASSERT_TRUE(machines::Resolve("pentagon", 512, out, error)) << error;
    EXPECT_EQ(out.name, "PENTAGON");
    EXPECT_EQ(out.kind, machines::NameKind::Model);
    EXPECT_EQ(out.ramKb, 512u);

    ASSERT_TRUE(machines::Resolve("profiplus", 0, out, error)) << error << ": an alias of a variant";
    EXPECT_EQ(out.name, "PROFI-PLUS");
    EXPECT_EQ(out.kind, machines::NameKind::Variant);
    ASSERT_NE(out.base, nullptr);

    ASSERT_TRUE(machines::Resolve("ZXPOLY-128K", 0, out, error)) << error;
    EXPECT_EQ(out.kind, machines::NameKind::ZxPoly);
    EXPECT_FALSE(machines::Resolve("ZXPOLY-128K", 512, out, error));
    EXPECT_NE(error.find("fixed"), std::string::npos) << error;

    EXPECT_FALSE(machines::Resolve("NO-SUCH", 0, out, error));
    EXPECT_NE(error.find("Unknown model"), std::string::npos) << error;
    EXPECT_FALSE(machines::Resolve("48KX", 0, out, error)) << "no prefix match";
    EXPECT_FALSE(machines::Resolve("GMX", 0, out, error)) << "in the table, not creatable";
    EXPECT_NE(error.find("not creatable"), std::string::npos) << error;
    EXPECT_FALSE(machines::Resolve("48K", 1024, out, error)) << "a RAM size the 48K has not";
}

TEST(MachineHistory_Test, TheNewestNameOnceAndThePreviousIsTheFirstOtherOne)
{
    machines::MachineHistory& history = machines::MachineHistory::Instance();
    const std::vector<machines::MachineHistory::Entry> saved = history.List();
    history.Clear();
    history.Note("128K", 0);
    history.Note("PENTAGON", 512);
    history.Note("128K", 0);
    ASSERT_EQ(history.List().size(), 2u) << "a name once";
    EXPECT_EQ(history.List().front().name, "128K");
    machines::MachineHistory::Entry previous;
    ASSERT_TRUE(history.Previous("128k", previous)) << "case-insensitive";
    EXPECT_EQ(previous.name, "PENTAGON");
    EXPECT_EQ(previous.ramKb, 512u);
    history.Clear();
    EXPECT_FALSE(history.Previous("128K", previous));
    for (auto it = saved.rbegin(); it != saved.rend(); ++it)
        history.Note(it->name, it->ramKb);
}

/// mach.start takes a variant by its alias; mach.previous goes back; the Profi's front panel flips. Boot-bound (three
/// machines created and started, the switch waiting for a frame boundary): about 90 ms
TEST_F(HostActionsMachine_Test, AVariantStartsByAliasThePreviousModelReturnsAndTheProfiTurboFlips)
{
    EXPECT_EQ(Run("mach.switch", Press({{"name", "turbo"}})).error, "Not Found") << "the Pentagon has no switch";
    EXPECT_EQ(Run("mach.switch", Press({{"name", "lever"}})).error, "Bad Request");

    machines::MachineHistory::Instance().Clear();
    const ActionResult profi = Run("mach.start", Press({{"model", "profiplus"}}));
    ASSERT_TRUE(profi.ok) << profi.message;
    EXPECT_EQ(profi.body.find("model")->s, "PROFI-PLUS");
    _emulator = nullptr;  // replaced
    _id = profi.body.find("emulator_id")->s;
    _extra.push_back(_id);

    const ActionResult turbo = Run("mach.switch", Press({{"name", "turbo"}, {"on", true}}));
    ASSERT_TRUE(turbo.ok) << turbo.message;
    EXPECT_TRUE(turbo.body.find("on")->b);
    EXPECT_EQ(ActionRegistry::Instance().ToggleState("mach.switch", _id, Press({{"name", "turbo"}}).params), true);
    const ReadoutSet switches = ReadReadouts(_id, {"machine.switches", "toggle.mach.switch.turbo"});
    ASSERT_TRUE(switches.unknown.empty());
    EXPECT_GE(Field(switches.values, {"machine.switches", "switches"})->size(), 1u);
    EXPECT_TRUE(Field(switches.values, {"toggle.mach.switch.turbo", "on"})->b);
    ASSERT_TRUE(Run("mach.switch", Press({{"name", "turbo"}})).ok) << "flips back";
    EXPECT_EQ(ActionRegistry::Instance().ToggleState("mach.switch", _id, Press({{"name", "turbo"}}).params), false);

    EXPECT_EQ(Run("mach.previous", Press()).error, "not_now") << "only one model so far";
    const ActionResult spectrum = Run("mach.start", Press({{"model", "48K"}}));
    ASSERT_TRUE(spectrum.ok) << spectrum.message;
    _id = spectrum.body.find("emulator_id")->s;
    _extra.push_back(_id);
    const ReadoutSet previous = ReadReadouts(_id, {"machine.previous"});
    EXPECT_EQ(Field(previous.values, {"machine.previous", "model"})->s, "PROFI-PLUS");

    const ActionResult back = Run("mach.previous", Press());
    ASSERT_TRUE(back.ok) << back.message;
    EXPECT_EQ(back.body.find("model")->s, "PROFI-PLUS");
    EXPECT_EQ(back.body.find("from")->s, "48K");
    _id = back.body.find("emulator_id")->s;
    _extra.push_back(_id);
}

/// endregion </Machine names and history>

/// region <Media>

/// Image-bound (a TR-DOS format of 80 x 2 tracks and three UDI writes): about 50 ms
TEST_F(HostActionsMachine_Test, ABlankTrdosDiskIsFormattedAndSavesAsAnotherFormat)
{
    BlankFloppySpec spec;
    spec.format = "tr-dos";
    std::unique_ptr<DiskImage> disk;
    ASSERT_TRUE(FloppyFormats::CreateBlank(false, spec, disk).Ok());
    EXPECT_EQ(spec.format, "trdos");
    EXPECT_EQ(spec.cylinders, 80);
    EXPECT_EQ(spec.sides, 2);
    EXPECT_TRUE(unrealdisk::TrdosFileSystem(*disk).IsTrdos()) << "formatted with an empty catalogue";

    const ActionResult created = Run("media.create", Press({{"drive", "B"}, {"format", "trdos"}, {"cylinders", 40}}));
    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_EQ(created.body.find("format")->s, "trdos");
    EXPECT_EQ(created.body.find("cylinders")->i, 40);
    EXPECT_EQ(Run("media.create", Press({{"drive", "B"}, {"format", "cpm"}})).error, "Bad Request");

    const std::string folder = Scratch("media-export");
    const std::string udi = FileHelper::PathCombine(folder, "blank.udi");
    const ActionResult exported = Run("media.export", Press({{"drive", "B"}, {"format", "udi"}, {"path", udi}}));
    ASSERT_TRUE(exported.ok) << exported.message;
    EXPECT_TRUE(FileHelper::FileExists(udi));
    EXPECT_EQ(Run("media.export", Press({{"drive", "B"}, {"path", udi}})).error, "Conflict") << "needs force";
    EXPECT_TRUE(Run("media.export", Press({{"drive", "B"}, {"path", udi}, {"force", true}})).ok);
    EXPECT_EQ(Run("media.export", Press({{"drive", "B"}, {"format", "tzx"}, {"path", udi}})).error, "Bad Request")
        << "a disk is not written as a tape";
    EXPECT_EQ(Run("media.export", Press({{"drive", "C"}, {"format", "trd"}})).error, "Conflict") << "empty";
}

/// Image-bound (two TRD files loaded twice each, a blank disk built): about 90 ms
TEST_F(HostActionsMachine_Test, TwoDrivesExchangeTheirDisksAndADiscardDropsTheWrites)
{
    const std::string a = Testdata("loaders/trd/EyeAche.trd");
    const std::string b = Testdata("sound/The_Viewer1.0.trd");
    ASSERT_TRUE(Run("media.insert", Press({{"drive", "A"}, {"path", a}})).ok);
    ASSERT_TRUE(Run("media.insert", Press({{"drive", "B"}, {"path", b}, {"wp", true}})).ok);
    const ActionResult swapped = Run("media.swap", Press({{"drive", "A"}, {"with", "B"}}));
    ASSERT_TRUE(swapped.ok) << swapped.message;
    MediaManager* media = _emulator->GetContext()->pMediaManager;
    EXPECT_NE(media->Info("fdd.a")->source.find("The_Viewer"), std::string::npos) << media->Info("fdd.a")->source;
    EXPECT_NE(media->Info("fdd.b")->source.find("EyeAche"), std::string::npos);
    EXPECT_TRUE(media->Info("fdd.a")->writeProtect) << "the protect goes with the disk";
    EXPECT_EQ(Run("media.swap", Press({{"drive", "A"}})).error, "Bad Request") << "with or path";
    EXPECT_EQ(Run("media.swap", Press({{"drive", "A"}, {"with", "tape"}})).error, "Bad Request");

    // A blank disk has no file to go back to
    ASSERT_TRUE(Run("media.create", Press({{"drive", "C"}})).ok);
    const ActionResult blank = Run("media.swap", Press({{"drive", "C"}, {"with", "A"}}));
    EXPECT_EQ(blank.error, "Conflict") << blank.message;
    EXPECT_TRUE(Run("media.discard", Press({{"drive", "A"}})).ok);
    EXPECT_EQ(Run("media.discard", Press()).error, "Bad Request");
}

TEST_F(HostActionsMachine_Test, OpenedFilesAreRecentAndOneOpensAgainByIndex)
{
    RecentFiles& recent = RecentFiles::Instance();
    const std::string tape = Testdata("loaders/tap/AYtest_v0.2.tap");
    const std::string disk = Testdata("loaders/trd/EyeAche.trd");
    ASSERT_TRUE(Run("media.open", Press({{"path", tape}, {"autostart", false}})).ok);
    ASSERT_TRUE(Run("media.insert", Press({{"drive", "A"}, {"path", disk}})).ok);
    ASSERT_EQ(recent.List().size(), 2u);
    EXPECT_EQ(recent.List().front().kind, "disk") << "newest first";
    EXPECT_EQ(recent.List("tape").front().name, "AYtest_v0.2.tap");

    const ReadoutSet readout = ReadReadouts(_id, {"media.recent"});
    EXPECT_EQ(Field(readout.values, {"media.recent", "kinds", "tape"})->size(), 1u);
    EXPECT_EQ(Field(readout.values, {"media.recent", "all"})->items[0].find("name")->s, "EyeAche.trd");

    const ActionResult again = Run("media.open_recent", Press({{"index", 0}, {"kind", "tape"}, {"autostart", false}}));
    ASSERT_TRUE(again.ok) << again.message;
    EXPECT_EQ(again.body.find("kind")->s, "tape");
    EXPECT_EQ(recent.List().front().kind, "tape") << "opened again: to the front";
    EXPECT_EQ(Run("media.open_recent", Press({{"index", 5}, {"kind", "tape"}})).error, "Not Found");
    EXPECT_EQ(Run("media.open_recent", Press({{"kind", "floppies"}})).error, "Bad Request");
}

TEST(RecentFiles_Test, TheListKeepsTenPerKindAndComesBackFromItsStoreFile)
{
    RecentFiles& recent = RecentFiles::Instance();
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath("recent-files");
    ASSERT_TRUE(FileHelper::CreateFolders(folder));
    const std::string store = FileHelper::PathCombine(folder, "recent.json");
    ASSERT_TRUE(recent.SetStoreFile(store));
    recent.Clear();
    for (int i = 0; i < 12; i++)
        recent.Note("tape", FileHelper::PathCombine(folder, "t" + std::to_string(i) + ".tap"));
    recent.Note("snapshot", FileHelper::PathCombine(folder, "s.szx"));
    recent.Note("bogus", "x");
    EXPECT_EQ(recent.List("tape").size(), RecentFiles::kPerKind);
    EXPECT_EQ(recent.List("tape").front().name, "t11.tap");
    EXPECT_EQ(recent.List().size(), RecentFiles::kPerKind + 1) << "an unknown kind is ignored";
    EXPECT_TRUE(FileHelper::FileExists(store));

    ASSERT_TRUE(recent.SetStoreFile(""));
    recent.Clear();
    EXPECT_TRUE(recent.List().empty());
    ASSERT_TRUE(recent.SetStoreFile(store)) << "loads what was saved";
    EXPECT_EQ(recent.List().size(), RecentFiles::kPerKind + 1);
    EXPECT_EQ(recent.List().front().kind, "snapshot");
    ASSERT_TRUE(recent.SetStoreFile(""));
    recent.Clear();
}

/// endregion </Media>

/// region <RZX, input>

TEST_F(HostActionsMachine_Test, RzxPlayRefusesAMissingFileAndSeekNeedsAPlayback)
{
    EXPECT_EQ(Run("rzx.play", Press({{"path", Testdata("loaders/rzx/szx/no-such.rzx")}})).error, "Not Found");
    EXPECT_EQ(Run("rzx.play", Press()).error, "Bad Request");
    EXPECT_EQ(Run("rzx.seek", Press({{"frame", 10}})).error, "not_now");
    const ActionResult stopped = Run("rzx.stop", Press());
    ASSERT_TRUE(stopped.ok);
    EXPECT_FALSE(stopped.body.find("stopped")->b);
    const ReadoutSet readout = ReadReadouts(_id, {"media.rzx"});
    EXPECT_FALSE(Field(readout.values, {"media.rzx", "loaded"})->b);
}

TEST_F(HostActionsMachine_Test, AJoystickKindGoesOnAConnectorAndTheReadoutShowsIt)
{
    const ActionResult plugged = Run("input.joystick_type", Press({{"kind", "sinclair2"}}));
    ASSERT_TRUE(plugged.ok) << plugged.message;
    const std::string connector = plugged.body.find("connector")->s;
    const ReadoutSet readout = ReadReadouts(_id, {"input.connectors"});
    bool found = false;
    for (const StateNode& item : Field(readout.values, {"input.connectors", "connectors"})->items)
        if (item.find("id")->s == connector)
        {
            found = true;
            EXPECT_EQ(item.find("device")->s, "sinclair-joystick");
            EXPECT_EQ(item.find("port")->i, 2);
        }
    EXPECT_TRUE(found) << connector;
    EXPECT_TRUE(Run("input.joystick_type", Press({{"kind", "none"}, {"connector", connector}})).ok);
    EXPECT_EQ(Run("input.joystick_type", Press({{"kind", "none"}})).error, "Bad Request") << "which connector";
    EXPECT_EQ(Run("input.joystick_type", Press({{"kind", "trackball"}})).error, "Bad Request");
    EXPECT_EQ(Run("input.mouse_button", Press({{"button", "fourth"}})).error, "Bad Request");
}

/// endregion </RZX, input>

/// region <Recording>

TEST(RecordingPresets_Test, TheSixPresetsAreFoundByIdOrTitle)
{
    EXPECT_EQ(capture::RecordingPresets().size(), 6u);
    ASSERT_NE(capture::FindRecordingPreset("Gameplay (MP4)"), nullptr);
    EXPECT_STREQ(capture::FindRecordingPreset("Gameplay (MP4)")->id, "gameplay");
    EXPECT_STREQ(capture::FindRecordingPreset("MUSIC")->container, "flac");
    EXPECT_EQ(capture::FindRecordingPreset("nope"), nullptr);
    EXPECT_EQ(capture::RecordingPresetsValue().size(), 6u);
}

TEST_F(HostActionsMachine_Test, RecordToggleStartsAGifPausesAndStopsAndRefusesBadOptions)
{
    const std::string folder = Scratch("record-toggle");
    const std::string path = FileHelper::PathCombine(folder, "clip.gif");
    EXPECT_EQ(Run("cap.record_toggle", Press({{"preset", "nope"}})).error, "Bad Request");
    EXPECT_EQ(Run("cap.record_toggle", Press({{"mode", "split"}})).error, "Not Implemented");
    EXPECT_EQ(Run("cap.record_toggle", Press({{"format", "gif"}, {"audio", "aac"}, {"path", path}})).error,
              "Bad Request")
        << "GIF has no audio";

    const ActionResult started = Run("cap.record_toggle", Press({{"preset", "gif"}, {"path", path}}));
    ASSERT_TRUE(started.ok) << started.message;
    EXPECT_EQ(started.body.find("preset")->s, "gif");
    EXPECT_EQ(ActionRegistry::Instance().ToggleState("cap.record_toggle", _id), true);
    ASSERT_TRUE(Run("cap.record_pause", Press()).ok);
    EXPECT_EQ(ActionRegistry::Instance().ToggleState("cap.record_pause", _id), true);
    ASSERT_TRUE(Run("cap.record_pause", Press()).ok) << "continues";
    const ReadoutSet readout = ReadReadouts(_id, {"cap.record"});
    EXPECT_TRUE(Field(readout.values, {"cap.record", "recording"})->b);
    EXPECT_EQ(Field(readout.values, {"cap.record", "output"})->s, "clip.gif");

    const ActionResult stopped = Run("cap.record_toggle", Press());
    ASSERT_TRUE(stopped.ok) << stopped.message;
    EXPECT_FALSE(stopped.body.find("recording")->b);
    EXPECT_TRUE(FileHelper::FileExists(path));
    EXPECT_EQ(ActionRegistry::Instance().ToggleState("cap.record_toggle", _id), false);
    EXPECT_EQ(Run("cap.record_pause", Press()).error, "not_now") << "nothing to pause";

    // The same control on the other planes: presets and the status verb
    capture::RecordingControl control(*_emulator, folder);
    EXPECT_EQ(control.Execute("presets").body.find("presets")->size(), 6u);
    EXPECT_FALSE(control.Execute("status").body.find("recording")->b);
    EXPECT_EQ(control.Execute("stop").error, "Conflict");
    EXPECT_EQ(control.Execute("start", {{"bogus", "1"}}).error, "Bad Request");
}

/// The last seconds of a TTD recording as a GIF, the recording going on after it. Boot-bound (a machine started, a few
/// frames recorded and replayed twice for the GIFs): about 100 ms
TEST_F(HostActionsMachine_Test, ClipLastWritesTheEndOfTheRecordingAndTheRecordingGoesOn)
{
    EXPECT_EQ(Run("cap.clip_last", Press()).error, "not_now") << "no recording";
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->GetContext()->emulatorState.frame_counter > 1; }));
    ASSERT_TRUE(Run("run.pause", Press()).ok);
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsPaused(); }));
    const ActionResult start = Run("ttd.start", Press());
    ASSERT_TRUE(start.ok) << start.message;
    ASSERT_TRUE(Run("run.frames", Press({{"count", 6}})).ok);

    const std::string folder = Scratch("clip-last");
    const std::string path = FileHelper::PathCombine(folder, "last.gif");
    const ActionResult clip = Run("cap.clip_last", Press({{"seconds", 0.06}, {"path", path}}));
    ASSERT_TRUE(clip.ok) << clip.message;
    EXPECT_GE(clip.body.find("frames")->i, 1);
    EXPECT_TRUE(clip.body.find("recording_resumed")->b);
    EXPECT_TRUE(FileHelper::FileExists(path));
    EXPECT_TRUE(_emulator->GetContext()->pTimeTravelHooks->IsRecording()) << "the recording goes on";
    EXPECT_TRUE(_emulator->IsPaused()) << "the run state as it was";

    // A TTD range as a video, through the TTD verb (a stopped history)
    ASSERT_TRUE(Run("ttd.stop", Press()).ok);
    const std::string range = FileHelper::PathCombine(folder, "range.gif");
    const ActionResult exported = Run("ttd.export_clip", Press({{"from", clip.body.find("from")->i},
                                                                {"to", clip.body.find("to")->i},
                                                                {"path", range}}));
    ASSERT_TRUE(exported.ok) << exported.message;
    EXPECT_TRUE(FileHelper::FileExists(range));
    EXPECT_EQ(Run("ttd.export_clip", Press({{"from", 0}})).error, "Bad Request");
}

/// endregion </Recording>
