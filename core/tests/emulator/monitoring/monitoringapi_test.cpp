// monitoring::Call (core/src/emulator/monitoring/monitoringapi.h): the verbs every plane runs - `all`, the process's
// values, an instance's summaries by id or index, `breakpoint_groups`, the errors; the same names are surface
// readouts; host.clients counts what the planes report

#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/actions/readouts.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/monitoring/clientstats.h"
#include "emulator/monitoring/hostdevices.h"
#include "emulator/monitoring/monitoringapi.h"
#include "emulator/ports/deviceaccesswatch.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/windowview.h"

#include <algorithm>
#include <string>

class MonitoringApi_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Not started: summaries of a machine that does not run
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    StateNode Args() const
    {
        StateNode args = StateNode::Object();
        args["instance"] = _id;
        return args;
    }

    static int64_t Int(const StateNode& node, const char* name)
    {
        const StateNode* value = node.find(name);
        return value != nullptr ? value->i : -1;
    }

    Emulator* _emulator = nullptr;
    std::string _id;
};

TEST_F(MonitoringApi_Test, AllHasTheProcessAndEveryInstanceWithEachName)
{
    const monitoring::Reply reply = monitoring::Call("all", Args());
    ASSERT_TRUE(reply.Ok());
    ASSERT_NE(reply.body.find("host"), nullptr);
    ASSERT_NE(reply.body.find("clients"), nullptr);
    const StateNode* instances = reply.body.find("instances");
    ASSERT_NE(instances, nullptr);
    ASSERT_EQ(instances->items.size(), 1u) << "instance= keeps one";
    const StateNode& entry = instances->items[0];
    EXPECT_EQ(entry.find("instance")->s, _id);
    EXPECT_EQ(entry.find("model")->s, "PENTAGON");
    for (const std::string& name : monitoring::InstanceNames())
        EXPECT_NE(entry.find(name), nullptr) << name;
}

TEST_F(MonitoringApi_Test, AnInstancesNameAnswersItsFieldsAndUnknownsAreNotFound)
{
    monitoring::Reply stats = monitoring::Call("emulator.stats", Args());
    ASSERT_TRUE(stats.Ok());
    EXPECT_EQ(stats.body.find("instance")->s, _id);
    EXPECT_EQ(stats.body.find("state")->s, "stopped");
    const StateNode* cpu = stats.body.find("cpu");
    ASSERT_NE(cpu, nullptr);
    EXPECT_GT(cpu->find("mhz")->d, 3.0) << "a Pentagon's 3.5 MHz";
    EXPECT_NEAR(stats.body.find("nominal_fps")->d, 48.83, 0.01);

    const monitoring::Reply host = monitoring::Call("host.stats", StateNode::Object());
    ASSERT_TRUE(host.Ok());
    EXPECT_GT(Int(host.body, "rss_bytes"), 0);
    EXPECT_GE(Int(host.body, "threads"), 1);

    EXPECT_EQ(monitoring::Call("nope", Args()).status, 404);
    StateNode wrong = StateNode::Object();
    wrong["instance"] = "no-such-instance";
    EXPECT_EQ(monitoring::Call("emulator.stats", wrong).status, 404);
    StateNode byIndex = StateNode::Object();
    byIndex["instance"] = "0";
    const monitoring::Reply indexed = monitoring::Call("debug.analyzers", byIndex);
    ASSERT_TRUE(indexed.Ok()) << "an index names the instance too";
}

// The device access watch on every plane: the name carries the channels with their sites, `all` only how many; the
// value follows the watch's revision (a folded frame, a start, a stop)
TEST_F(MonitoringApi_Test, DevicesAccessCarriesTheChannelsAndAllTheirCount)
{
    monitoring::Reply off = monitoring::Call("devices.access", Args());
    ASSERT_TRUE(off.Ok());
    EXPECT_TRUE(off.body.find("available")->b);
    EXPECT_FALSE(off.body.find("active")->b);
    EXPECT_FALSE(off.body.find("continuous")->b);

    DeviceAccessWatch& watch = _emulator->GetContext()->pPortDecoder->GetAccessWatch();
    watch.Start();
    watch.Record(0x001F, 0xFF, false, false, 0x8000);  // IN #1F from 0x8000: the Pentagon's Kempston
    watch.EndFrame();
    const monitoring::Reply on = monitoring::Call("devices.access", Args());
    ASSERT_TRUE(on.Ok());
    EXPECT_TRUE(on.body.find("active")->b);
    EXPECT_EQ(Int(on.body, "frames_observed"), 1);
    const StateNode* channels = on.body.find("channels");
    ASSERT_NE(channels, nullptr);
    ASSERT_EQ(channels->items.size(), 1u);
    const StateNode& kempston = channels->items[0];
    EXPECT_EQ(kempston.find("device")->s, "kempston-joystick");
    EXPECT_EQ(kempston.find("input")->s, "kempston-joystick");
    EXPECT_EQ(kempston.find("dir")->s, "in");
    EXPECT_EQ(Int(kempston, "frames"), 1);
    const StateNode* sites = kempston.find("sites");
    ASSERT_NE(sites, nullptr);
    ASSERT_EQ(sites->items.size(), 1u);
    EXPECT_EQ(Int(sites->items[0], "pc"), 0x8000);

    const monitoring::Reply all = monitoring::Call("all", Args());
    ASSERT_TRUE(all.Ok());
    const StateNode* summary = all.body.find("instances")->items[0].find("devices.access");
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(Int(*summary, "channel_count"), 1);
    EXPECT_EQ(summary->find("channels"), nullptr) << "the list stays with the name";

    watch.Stop();
    EXPECT_FALSE(monitoring::Call("devices.access", Args()).body.find("active")->b) << "a new revision";
}

TEST_F(MonitoringApi_Test, BreakpointGroupsListEachGroupWithItsIds)
{
    BreakpointManager* breakpoints = _emulator->GetContext()->pDebugManager->GetBreakpointsManager();
    const uint16_t a = breakpoints->AddExecutionBreakpoint(0x8000);
    const uint16_t b = breakpoints->AddExecutionBreakpoint(0x8003);
    ASSERT_TRUE(breakpoints->SetBreakpointGroup(b, "loader"));
    ASSERT_TRUE(breakpoints->DeactivateBreakpoint(b));
    const monitoring::Reply reply = monitoring::Call("breakpoint_groups", Args());
    ASSERT_TRUE(reply.Ok());
    const StateNode* groups = reply.body.find("groups");
    ASSERT_NE(groups, nullptr);
    ASSERT_EQ(groups->items.size(), 2u);
    EXPECT_EQ(groups->items[0].find("name")->s, "default");
    EXPECT_EQ(groups->items[0].find("ids")->items[0].i, a);
    EXPECT_EQ(groups->items[1].find("name")->s, "loader");
    EXPECT_EQ(Int(groups->items[1], "total"), 1);
    EXPECT_EQ(Int(groups->items[1], "active"), 0);
    EXPECT_EQ(reply.body.find("cpu")->s, "main");

    // One CPU's groups: the sound card CPU's are its own (none here), an unknown CPU is refused
    StateNode card = Args();
    card["cpu"] = "gs";
    const monitoring::Reply gs = monitoring::Call("breakpoint_groups", card);
    ASSERT_TRUE(gs.Ok());
    EXPECT_EQ(gs.body.find("cpu")->s, "gs");
    EXPECT_TRUE(gs.body.find("groups")->items.empty());
    card["cpu"] = "z81";
    EXPECT_EQ(monitoring::Call("breakpoint_groups", card).status, 400);

    // The summary follows the change (the cache is keyed by the manager's revision)
    const monitoring::Reply summary = monitoring::Call("debug.breakpoints", Args());
    EXPECT_EQ(Int(summary.body, "total"), 2);
    EXPECT_EQ(Int(summary.body, "inactive"), 1);
    breakpoints->ClearBreakpoints();
    EXPECT_EQ(Int(monitoring::Call("debug.breakpoints", Args()).body, "total"), 0);
}

TEST_F(MonitoringApi_Test, TheNamesAreSurfaceReadoutsAndMediaDiskSaysDirty)
{
    const std::vector<std::string>& names = actions::BaseReadoutNames();
    for (const char* name : {"host.stats", "host.clients", "emulator.stats", "debug.breakpoints", "media.slots"})
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << name;
    const StateNode stats = actions::ReadReadout(*_emulator, "emulator.stats");
    EXPECT_NE(stats.find("fps"), nullptr);
    const StateNode disk = actions::ReadReadout(*_emulator, "media.disk");
    const StateNode* drives = disk.find("drives");
    ASSERT_NE(drives, nullptr);
    ASSERT_FALSE(drives->items.empty()) << "a Pentagon has Beta 128 drives";
    EXPECT_NE(drives->items[0].find("dirty"), nullptr);
}

/// controllers.devices is the process's: the owner's published devices, available false without an owner
TEST(MonitoringApiDevices_Test, ControllersDevicesIsWhatTheOwnerPublished)
{
    monitoring::HostDevices& board = monitoring::HostDevices::Instance();
    board.Withdraw();
    EXPECT_TRUE(monitoring::IsProcessName("controllers.devices"));
    monitoring::Reply reply = monitoring::Call("controllers.devices", StateNode::Object());
    ASSERT_TRUE(reply.Ok());
    EXPECT_FALSE(reply.body.find("available")->b) << "no host input owner in the test";
    EXPECT_EQ(reply.body.find("devices"), nullptr);

    StateNode devices = StateNode::Array();
    StateNode pad = StateNode::Object();
    pad["id"] = "fake:1";
    devices.push(pad);
    board.Publish(devices, 7);
    reply = monitoring::Call("controllers.devices", StateNode::Object());
    ASSERT_TRUE(reply.Ok());
    EXPECT_TRUE(reply.body.find("available")->b);
    EXPECT_EQ(reply.body.find("revision")->i, 7);
    ASSERT_EQ(reply.body.find("devices")->items.size(), 1u);
    EXPECT_EQ(reply.body.find("devices")->items[0].find("id")->s, "fake:1");
    board.Withdraw();
    EXPECT_FALSE(monitoring::ReadProcess("controllers.devices").find("available")->b);
}

TEST_F(MonitoringApi_Test, HostClientsCountWhatThePlanesReport)
{
    monitoring::ClientStats& clients = monitoring::ClientStats::Instance();
    const monitoring::ClientCounts before = clients.Read(monitoring::ClientPlane::Cli);
    {
        const monitoring::ClientConnection connection(monitoring::ClientPlane::Cli);
        clients.Request(monitoring::ClientPlane::Cli);
        const monitoring::Reply reply = monitoring::Call("host.clients", StateNode::Object());
        const StateNode* cli = reply.body.find("cli");
        ASSERT_NE(cli, nullptr);
        EXPECT_EQ(Int(*cli, "connections"), before.connections + 1);
        EXPECT_EQ(Int(*cli, "requests"), static_cast<int64_t>(before.requests) + 1);
        EXPECT_GE(Int(reply.body, "instances"), 1);
    }
    EXPECT_EQ(clients.Read(monitoring::ClientPlane::Cli).connections, before.connections) << "closed";
    EXPECT_EQ(clients.Read(monitoring::ClientPlane::Cli).opened, before.opened + 1);
}

TEST_F(MonitoringApi_Test, VideoPresentIsThePublishedWindowWithoutCounters)
{
    EXPECT_FALSE(monitoring::Call("video.present", StateNode::Object()).body.find("available")->b) << "headless";

    WindowDisplaySettings settings;
    settings.emulatorId = _id;
    settings.presentPolicy = "auto";
    settings.displayRate = "match";
    settings.present.presenter = "metal";
    settings.present.vrr = false;
    settings.present.minRefreshHz = settings.present.maxRefreshHz = 60.0;
    settings.present.presented = 1234;
    settings.present.displayState = "matched";
    settings.present.displayHz = 50.0;
    settings.present.originalHz = 60.0;
    settings.present.machineHz = 48.828;
    WindowView::Publish(settings);
    const monitoring::Reply reply = monitoring::Call("video.present", StateNode::Object());
    WindowView::Publish(std::nullopt);
    ASSERT_TRUE(reply.Ok());
    EXPECT_TRUE(reply.body.find("available")->b);
    EXPECT_EQ(reply.body.find("presenter")->s, "metal");
    EXPECT_EQ(reply.body.find("presented"), nullptr) << "counters change every frame: a topic would send each second";
    const StateNode* display = reply.body.find("display");
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->find("setting")->s, "match");
    EXPECT_EQ(display->find("state")->s, "matched");
    EXPECT_DOUBLE_EQ(display->find("original_hz")->d, 60.0);
    EXPECT_DOUBLE_EQ(display->find("machine_hz")->d, 48.83);
    EXPECT_NE(monitoring::Call("all", Args()).body.find("video"), nullptr);
}

/// MCP tool calls per tool and action (the dispatcher's observer feeds ClientStats::ToolCall): host.clients.mcp.tools
TEST_F(MonitoringApi_Test, HostClientsCountMcpToolCallsPerAction)
{
    monitoring::ClientStats& clients = monitoring::ClientStats::Instance();
    const auto before = clients.ReadToolCalls();
    const uint64_t explainsBefore =
        before.count("analysis") && before.at("analysis").actions.count("explain") ? before.at("analysis").actions.at("explain") : 0;
    clients.ToolCall("analysis", "explain");
    clients.ToolCall("analysis", "explain");
    clients.ToolCall("analysis", "status");
    clients.ToolCall("search_api", "");

    const monitoring::Reply reply = monitoring::Call("host.clients", StateNode::Object());
    const StateNode* mcp = reply.body.find("mcp");
    ASSERT_NE(mcp, nullptr);
    const StateNode* tools = mcp->find("tools");
    ASSERT_NE(tools, nullptr);
    const StateNode* analysis = tools->find("analysis");
    ASSERT_NE(analysis, nullptr);
    const StateNode* actions = analysis->find("actions");
    ASSERT_NE(actions, nullptr);
    EXPECT_EQ(Int(*actions, "explain"), static_cast<int64_t>(explainsBefore) + 2);
    EXPECT_GE(Int(*analysis, "calls"), 3);
    const StateNode* search = tools->find("search_api");
    ASSERT_NE(search, nullptr);
    EXPECT_GE(Int(*search->find("actions"), "(none)"), 1);
}
