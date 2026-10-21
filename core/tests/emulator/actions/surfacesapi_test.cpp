// The surface verbs every plane calls (actions::surfaces::Call; Stream Deck integration targeting.md, protocol.md
// §2-§4): views and the followed instance (T-1: "no id" on the WebAPI), sessions and pins, the target object, the
// readout vocabulary and the activity feed, over the process's TargetState with one real (not started) instance.

#include "pch.h"

#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/readouts.h"
#include "emulator/actions/surfacesapi.h"
#include "emulator/actions/targetstate.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

using namespace actions;

namespace
{
StateNode Args(std::initializer_list<std::pair<const char*, StateNode>> members)
{
    StateNode args = StateNode::Object();
    for (const auto& [name, value] : members)
        args[name] = value;
    return args;
}

class SurfacesApi_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Not started: nothing here runs the CPU
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
    }

    void TearDown() override
    {
        for (const std::string& session : _sessions)
            surfaces::Call("session_close", Args({{"session", session}}));
        for (const std::string& view : _views)
            surfaces::Call("view_remove", Args({{"view", view}}));
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::string AddView(const std::string& name, const std::string& active)
    {
        const surfaces::Reply reply = surfaces::Call("view_add", Args({{"name", name}, {"active", active}}));
        EXPECT_TRUE(reply.Ok()) << StateNodeText(reply.body);
        if (!reply.Ok())
            return {};
        const std::string id = reply.body.find("view")->find("id")->s;
        _views.push_back(id);
        return id;
    }

    std::string OpenSession(const std::string& name)
    {
        const surfaces::Reply reply = surfaces::Call("session_open", Args({{"name", name}}));
        EXPECT_TRUE(reply.Ok());
        if (!reply.Ok())
            return {};
        const std::string id = reply.body.find("session")->find("id")->s;
        _sessions.push_back(id);
        return id;
    }

    static std::string StateNodeText(const StateNode& node)
    {
        const StateNode* message = node.find("message");
        return message != nullptr ? message->s : std::string();
    }

    Emulator* _emulator = nullptr;
    std::string _id;
    std::vector<std::string> _views;
    std::vector<std::string> _sessions;
};
}  // namespace

TEST_F(SurfacesApi_Test, NoIdFollowsTheFocusedViewWhenThereAreViews)
{
    std::string error;
    const size_t instances = EmulatorManager::GetInstance()->GetEmulatorIds().size();
    if (instances == 1)
    {
        EXPECT_EQ(surfaces::DefaultInstance(error), _id) << "headless: the sole instance";
    }

    const std::string view = AddView("Test window", _id);
    EXPECT_EQ(surfaces::DefaultInstance(error), _id) << "T-1: the followed instance";
    const surfaces::Reply views = surfaces::Call("views", StateNode::Object());
    ASSERT_TRUE(views.Ok());
    const StateNode& followed = *views.body.find("followed");
    EXPECT_EQ(followed.find("instance")->s, _id);
    EXPECT_EQ(followed.find("model")->s, "PENTAGON");
    EXPECT_EQ(followed.find("view")->find("id")->s, view);
    EXPECT_EQ(followed.find("mode")->s, "follow");
    EXPECT_EQ(followed.find("source")->s, "view");

    // An empty view in front is skipped (rule 3); removing a view that is not the plane's is refused
    const std::string empty = AddView("Empty window", "");
    EXPECT_TRUE(surfaces::Call("view_update", Args({{"view", empty}, {"activate", true}})).Ok());
    EXPECT_EQ(surfaces::DefaultInstance(error), _id);
    EXPECT_EQ(surfaces::Call("view_update", Args({{"view", "v999999"}})).status, 404);
    EXPECT_EQ(surfaces::Call("view_add", Args({{"name", "x"}, {"active", "no-such-instance"}})).status, 404);
}

TEST_F(SurfacesApi_Test, SessionsArePinnedAndReportTheirTarget)
{
    const std::string view = AddView("Test window", _id);
    const std::string session = OpenSession("REST surface");
    surfaces::Reply reply = surfaces::Call("pin", Args({{"session", session}, {"target", "view:" + view}}));
    ASSERT_TRUE(reply.Ok()) << StateNodeText(reply.body);
    EXPECT_EQ(reply.body.find("session")->find("pin")->s, "view:" + view);
    EXPECT_EQ(reply.body.find("session")->find("target")->find("instance")->s, _id);

    StateNode byObject = StateNode::Object();
    byObject["instance"] = _id;
    reply = surfaces::Call("pin", Args({{"session", session}, {"target", byObject}}));
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(reply.body.find("session")->find("target")->find("mode")->s, "instance");

    reply = surfaces::Call("target", Args({{"session", session}}));
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(reply.body.find("target")->find("instance")->s, _id);

    EXPECT_EQ(surfaces::Call("pin", Args({{"session", session}, {"target", "view:v999999"}})).status, 404);
    EXPECT_EQ(surfaces::Call("pin", Args({{"session", session}, {"target", "window:1"}})).status, 400);
    EXPECT_EQ(surfaces::Call("pin", Args({{"session", "s999999"}, {"target", "follow"}})).status, 404);
    EXPECT_EQ(surfaces::Call("session_open", StateNode::Object()).status, 400) << "a name is needed";
    EXPECT_EQ(surfaces::Call("nonsense", StateNode::Object()).status, 400);

    // The view closes: the view pin falls back to following, with a notice
    ASSERT_TRUE(surfaces::Call("pin", Args({{"session", session}, {"target", "view:" + view}})).Ok());
    ASSERT_TRUE(surfaces::Call("view_remove", Args({{"view", view}})).Ok());
    _views.clear();
    reply = surfaces::Call("session", Args({{"session", session}}));
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(reply.body.find("session")->find("pin")->s, "follow");
    EXPECT_NE(reply.body.find("session")->find("notice"), nullptr);
}

TEST_F(SurfacesApi_Test, TheReadoutVocabularyReadsPublishedState)
{
    const ReadoutSet all = ReadReadouts(_id, {});
    for (const std::string& name : BaseReadoutNames())
        EXPECT_NE(all.values.find(name), nullptr) << name;
    EXPECT_EQ(all.values.find("run.state")->find("state")->s, "stopped");
    EXPECT_EQ(all.values.find("machine")->find("model")->s, "PENTAGON");
    EXPECT_EQ(all.values.find("machine")->find("ram_kb")->i, 128);
    EXPECT_NE(all.values.find("toggle.mach.turbo_toggle"), nullptr);
    EXPECT_FALSE(all.values.find("toggle.mach.turbo_toggle")->find("on")->b);
    EXPECT_NE(all.values.find("toggle.feature.turbomode"), nullptr) << "a toggle per feature";
    EXPECT_FALSE(all.values.find("media.tape")->find("loaded")->b);

    const ReadoutSet media = ReadReadouts(_id, {"media.*", "toggle.set.fast_tape", "no.such"});
    EXPECT_NE(media.values.find("media.tape"), nullptr);
    EXPECT_NE(media.values.find("media.disk"), nullptr);
    EXPECT_EQ(media.values.find("run.state"), nullptr);
    EXPECT_NE(media.values.find("toggle.set.fast_tape"), nullptr) << "a setting is read by its name";
    ASSERT_EQ(media.unknown.size(), 1u);
    EXPECT_EQ(media.unknown[0], "no.such");
    const StateNode* drives = media.values.find("media.disk")->find("drives");
    ASSERT_NE(drives, nullptr);
    ASSERT_FALSE(drives->items.empty()) << "a Pentagon has Beta 128 drives";
    EXPECT_EQ(drives->items[0].find("letter")->s, "A");

    const surfaces::Reply reply = surfaces::Call("readouts", Args({{"instance", _id}, {"names", "run.state,debug.mode"}}));
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(reply.body.find("values")->size(), 2u);
    EXPECT_TRUE(ReadoutMatches("*", "machine"));
    EXPECT_TRUE(ReadoutMatches("toggle.*", "toggle.feature.debugmode"));
    EXPECT_FALSE(ReadoutMatches("media.*", "machine"));
}

TEST_F(SurfacesApi_Test, TheActivityFeedRecordsWhatTheWorkerRan)
{
    const uint64_t since = ActivityFeed::Instance().LastSeq();
    ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    ActionValue press;
    const ActionResult result = ActionRegistry::Instance().Run("dbg.breakpoint_hits_reset", press, _id, origin);
    ASSERT_TRUE(result.ok) << result.message;
    const surfaces::Reply reply = surfaces::Call("activity", Args({{"since", static_cast<int64_t>(since)},
                                                                   {"instance", _id}}));
    ASSERT_TRUE(reply.Ok());
    const StateNode& entries = *reply.body.find("entries");
    ASSERT_EQ(entries.items.size(), 1u);
    EXPECT_EQ(entries.items[0].find("id")->s, "dbg.breakpoint_hits_reset");
    EXPECT_EQ(entries.items[0].find("origin")->s, "gui");
    EXPECT_EQ(entries.items[0].find("surface")->s, "GUI");
    EXPECT_TRUE(entries.items[0].find("ok")->b);
    EXPECT_EQ(surfaces::Call("activity", Args({{"since", "x"}})).status, 400);
}
