// DebugEventHub: /api/v1/websocket's debugger events (protocol.md §5), driven without a network: each
// "connection" is a send function collecting what the hub sends it

#include "pch.h"

#include "debug-event-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

#include <json/json.h>

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr const char* kFirst = "123e4567-e89b-12d3-a456-426614174000";
constexpr const char* kSecond = "223e4567-e89b-12d3-a456-426614174001";

/// What one client received, parsed
class Inbox
{
public:
    DebugEventHub::Send Sender()
    {
        return [this](const std::string& text) {
            Json::Value value;
            Json::CharReaderBuilder reader;
            std::string errors;
            std::istringstream stream(text);
            Json::parseFromStream(reader, stream, &value, &errors);
            std::lock_guard<std::mutex> lock(_mutex);
            _messages.push_back(value);
        };
    }
    std::vector<Json::Value> Messages()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _messages;
    }
    std::vector<Json::Value> Events()
    {
        std::vector<Json::Value> events;
        for (const Json::Value& message : Messages())
            if (message["op"].asString() == "event")
                events.push_back(message);
        return events;
    }

private:
    std::mutex _mutex;
    std::vector<Json::Value> _messages;
};

void PostPause(const char* emulator, bool breakpoint)
{
    auto* payload = new EmulatorStateChangePayload(std::string(emulator), StatePaused);
    if (breakpoint)
    {
        payload->pauseCause = PauseCause::Breakpoint;
        payload->breakpointId = 3;
        payload->address = 0x8005;
        payload->hitKind = BreakpointHitKind::Execute;
    }
    MessageCenter::DefaultMessageCenter().Post(NC_EMULATOR_STATE_CHANGE, payload, true);
}

void PostStepDone(const char* emulator, bool stopped)
{
    auto* payload = new CpuStepPayload(std::string(emulator));
    payload->stopped = stopped;
    payload->breakpointId = 7;
    payload->address = 0x9000;
    payload->hitKind = BreakpointHitKind::MemoryWrite;
    MessageCenter::DefaultMessageCenter().Post(NC_EXECUTION_CPU_STEP, payload, true);
}

class DebugEventHub_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _hub.Start();
    }
    void TearDown() override
    {
        _hub.Stop();
        MessageCenter::DisposeDefaultMessageCenter();
    }

    DebugEventHub _hub;
};
}  // namespace

TEST_F(DebugEventHub_Test, SubscribeReplyNamesTheSeqAndTheTopicsNotPublished)
{
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s1","emulator":")" + std::string(kFirst) +
                                   R"(","topics":["debug","timeline"]})",
                       inbox.Sender());
    const auto messages = inbox.Messages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0]["op"].asString(), "subscribed");
    EXPECT_EQ(messages[0]["id"].asString(), "s1");
    EXPECT_EQ(messages[0]["seq"].asUInt64(), 0u);
    ASSERT_EQ(messages[0]["unsupported"].size(), 1u);
    EXPECT_EQ(messages[0]["unsupported"][0].asString(), "timeline");
}

TEST_F(DebugEventHub_Test, EventsReachTheMatchingSubscriptionsNumberedPerEmulator)
{
    Inbox first, all;
    _hub.HandleMessage(&first, R"({"op":"subscribe","id":"a","emulator":")" + std::string(kFirst) + R"("})",
                       first.Sender());
    _hub.HandleMessage(&all, R"({"op":"subscribe","id":"b","topics":["debug"]})", all.Sender());

    PostPause(kFirst, true);       // a breakpoint pause of the first emulator
    PostStepDone(kSecond, false);  // a step of the second
    PostStepDone(kFirst, true);    // a step of the first ended by a watchpoint
    ASSERT_TRUE(TestWait::For([&] { return all.Events().size() == 3; }, std::chrono::seconds(2)));
    ASSERT_TRUE(TestWait::For([&] { return first.Events().size() == 2; }, std::chrono::seconds(2)));

    const auto events = first.Events();
    EXPECT_EQ(events[0]["event"].asString(), "paused");
    EXPECT_EQ(events[0]["reason"].asString(), "breakpoint");
    EXPECT_EQ(events[0]["breakpoint_id"].asUInt(), 3u);
    EXPECT_EQ(events[0]["address"].asUInt(), 0x8005u);
    EXPECT_EQ(events[0]["access"].asString(), "execute");
    EXPECT_EQ(events[0]["emulator"].asString(), kFirst);
    EXPECT_EQ(events[0]["topic"].asString(), "debug");
    EXPECT_EQ(events[0]["seq"].asUInt64(), 1u);
    EXPECT_EQ(events[1]["event"].asString(), "step_done");
    EXPECT_EQ(events[1]["reason"].asString(), "breakpoint");
    EXPECT_EQ(events[1]["access"].asString(), "write");
    EXPECT_EQ(events[1]["seq"].asUInt64(), 2u) << "numbered per emulator: the second's step is not counted";

    const auto everything = all.Events();
    EXPECT_EQ(everything[1]["emulator"].asString(), kSecond);
    EXPECT_EQ(everything[1]["reason"].asString(), "step");
    EXPECT_EQ(everything[1]["seq"].asUInt64(), 1u);
}

TEST_F(DebugEventHub_Test, UnsubscribeAndClosingStopTheEvents)
{
    Inbox kept, left, closed;
    for (Inbox* inbox : {&kept, &left, &closed})
        _hub.HandleMessage(inbox, R"({"op":"subscribe","id":"x"})", inbox->Sender());
    _hub.HandleMessage(&left, R"({"op":"unsubscribe","id":"x"})", left.Sender());
    _hub.RemoveClient(&closed);

    PostPause(kFirst, false);
    ASSERT_TRUE(TestWait::For([&] { return kept.Events().size() == 1; }, std::chrono::seconds(2)));
    EXPECT_EQ(kept.Events()[0]["reason"].asString(), "pause");
    EXPECT_TRUE(left.Events().empty());
    EXPECT_TRUE(closed.Events().empty());
    EXPECT_EQ(left.Messages().back()["op"].asString(), "unsubscribed");
}

TEST_F(DebugEventHub_Test, BadRequestsGetAnError)
{
    Inbox inbox;
    _hub.HandleMessage(&inbox, "not json", inbox.Sender());
    _hub.HandleMessage(&inbox, R"({"op":"dance","id":"q"})", inbox.Sender());
    const auto messages = inbox.Messages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0]["op"].asString(), "error");
    EXPECT_EQ(messages[1]["op"].asString(), "error");
    EXPECT_EQ(messages[1]["id"].asString(), "q");
}

TEST_F(DebugEventHub_Test, BreakpointsChangedCarriesTheIds)
{
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s"})", inbox.Sender());
    auto* payload = new BreakpointsChangedPayload(std::string(kFirst));
    payload->ids = {2, 5};
    MessageCenter::DefaultMessageCenter().Post(NC_BREAKPOINTS_CHANGED, payload, true);
    ASSERT_TRUE(TestWait::For([&] { return inbox.Events().size() == 1; }, std::chrono::seconds(2)));
    const Json::Value event = inbox.Events()[0];
    EXPECT_EQ(event["event"].asString(), "breakpoints_changed");
    EXPECT_EQ(event["cpu"].asString(), "main");
    ASSERT_EQ(event["ids"].size(), 2u);
    EXPECT_EQ(event["ids"][0].asUInt(), 2u);
    EXPECT_EQ(event["ids"][1].asUInt(), 5u);
}
