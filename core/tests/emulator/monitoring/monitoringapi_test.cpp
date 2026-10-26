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
#include "emulator/monitoring/monitoringapi.h"

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
