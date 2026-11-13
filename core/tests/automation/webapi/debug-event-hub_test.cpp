// DebugEventHub: /api/v1/websocket's debugger events (protocol.md §5), driven without a network: each
// "connection" is a send function collecting what the hub sends it

#include "pch.h"

#include "debug-event-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "debugger/analyzers/sectiontimer/sectiontimeranalyzer.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/watch/framewatch.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulator.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <iostream>
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

/// NC_SCRIPT_CALLBACKS_CHANGED: callbacks_changed on debug with the callback as GET /callbacks shows it, then removed
TEST_F(DebugEventHub_Test, ScriptCallbacksChangedCarriesTheCallbackOrItsRemoval)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    ScriptCallbackManager* callbacks = emulator->GetContext()->pDebugManager->GetScriptCallbacks();
    ASSERT_NE(callbacks, nullptr);
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto changes = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString() == "callbacks_changed")
                found.push_back(event);
        return found;
    };

    CallbackSpec spec;
    spec.trigger = CallbackTrigger::GameEvent;
    spec.event = "died";
    spec.owner = "lua";
    spec.label = "deaths";
    std::string error;
    const uint32_t id = callbacks->Register(spec, error);
    ASSERT_NE(id, 0u) << error;
    ASSERT_TRUE(TestWait::For([&] { return changes().size() == 1; }, std::chrono::seconds(2)));
    Json::Value event = changes()[0];
    EXPECT_EQ(event["emulator"].asString(), emulator->GetId());
    EXPECT_EQ(event["id"].asUInt(), id);
    EXPECT_EQ(event["callback"]["label"].asString(), "deaths");
    EXPECT_EQ(event["callback"]["state"].asString(), "active");
    EXPECT_EQ(event["callback"]["target"].asString(), "event died");
    EXPECT_FALSE(event.isMember("removed"));

    ASSERT_TRUE(callbacks->Remove(id));
    ASSERT_TRUE(TestWait::For([&] { return changes().size() == 2; }, std::chrono::seconds(2)));
    event = changes()[1];
    EXPECT_EQ(event["id"].asUInt(), id);
    EXPECT_TRUE(event["removed"].asBool());
    EXPECT_FALSE(event.isMember("callback"));
    EXPECT_GT(event["seq"].asUInt64(), changes()[0]["seq"].asUInt64());
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// NC_FRAME_WATCH: watch_added, then watch_hit (frame, reasons, removed: a once watch) when a direct run passes it
TEST_F(DebugEventHub_Test, FrameWatchEventsCarryTheHit)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto watchEvents = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString().rfind("watch_", 0) == 0)
                found.push_back(event);
        return found;
    };
    // FRAME is the frame counter: two frames on
    const uint64_t frame = context->emulatorState.frame_counter;
    const framewatch::Outcome added =
        framewatch::AddWatch(context, {{"if", "FRAME >= " + std::to_string(frame + 2)}, {"label", "two"}});
    ASSERT_EQ(added.status, 201) << added.text;
    emulator->RunNFrames(4);
    ASSERT_TRUE(TestWait::For([&] { return watchEvents().size() == 2; }, std::chrono::seconds(2)));
    const std::vector<Json::Value> events = watchEvents();
    EXPECT_EQ(events[0]["event"].asString(), "watch_added");
    EXPECT_EQ(events[0]["label"].asString(), "two");
    EXPECT_EQ(events[1]["event"].asString(), "watch_hit");
    EXPECT_EQ(events[1]["reasons"][0].asString(), "if");
    EXPECT_TRUE(events[1]["removed"].asBool()) << "a once watch goes with its hit";
    EXPECT_FALSE(events[1]["paused"].asBool()) << "a direct run is not paused by a watch";
    EXPECT_GE(events[1]["frame"].asUInt64(), frame + 2);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// NC_SECTION_TIMER: section_timer_started, section_timer_break with the pass that stopped the machine, then
/// section_timer_cleared and section_timer_stopped; a run that skips breakpoints sends no break
TEST_F(DebugEventHub_Test, SectionTimerEventsCarryTheStop)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // EI; Work: LD B,#40; DJNZ $; Work.End: NOP; JR Work - 834 T; IM 2 to EI : RETI at #A0A0 (37 T an interrupt)
    const uint8_t loop[] = {0xFB, 0x06, 0x40, 0x10, 0xFE, 0x00, 0x18, 0xF9};
    for (size_t i = 0; i < sizeof(loop); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
    const uint8_t handler[] = {0xFB, 0xED, 0x4D};
    for (size_t i = 0; i < sizeof(handler); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xA0A0 + i), handler[i]);
    for (uint32_t a = 0x9000; a <= 0x9100; a++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0xA0);
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->sp = 0xBF00;
    z80->i = 0x90;
    z80->im = 2;
    z80->iff1 = z80->iff2 = 0;
    z80->halted = 0;

    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto timerEvents = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString().rfind("section_timer_", 0) == 0)
                found.push_back(event);
        return found;
    };
    SectionTimerAnalyzer* timer = SectionTimerAnalyzer::Find(context);
    ASSERT_NE(timer, nullptr);
    SectionTimerAnalyzer::Spec spec;
    spec.sections = {"Work=#8001:#8005"};
    spec.breakIf = {"Work>834"};
    std::string error;
    ASSERT_TRUE(timer->Start(spec, error)) << error;
    emulator->RunNFrames(2);  // skips breakpoints: no stop, no break
    const uint64_t frameT = context->GetFrameTStates();
    const uint64_t skippedUntil = context->emulatorState.frame_counter * frameT + z80->t;
    emulator->RunNFrames(3, /*skipBreakpoints=*/false);
    ASSERT_TRUE(emulator->LastDirectStop().hit);
    timer->Clear();
    EXPECT_TRUE(timer->Stop());
    ASSERT_TRUE(TestWait::For([&] { return timerEvents().size() == 4; }, std::chrono::seconds(2)))
        << timerEvents().size();
    const std::vector<Json::Value> events = timerEvents();
    EXPECT_EQ(events[0]["event"].asString(), "section_timer_started");
    EXPECT_EQ(events[1]["event"].asString(), "section_timer_break");
    EXPECT_EQ(events[1]["condition"].asString(), "Work>834");
    EXPECT_EQ(events[1]["section"].asString(), "Work");
    EXPECT_EQ(events[1]["elapsed"].asUInt64(), 834u + 37u);
    EXPECT_EQ(events[1]["net"].asUInt64(), 834u);
    EXPECT_EQ(events[1]["interrupts"].asUInt(), 1u);
    const uint64_t exitAt =
        events[1]["frame"].asUInt64() * frameT + events[1]["t"].asUInt64() + events[1]["elapsed"].asUInt64();
    EXPECT_GE(exitAt, skippedUntil) << "the slow passes of the run that skips breakpoints sent no break";
    EXPECT_EQ(events[2]["event"].asString(), "section_timer_cleared");
    EXPECT_EQ(events[3]["event"].asString(), "section_timer_stopped");
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(DebugEventHub_Test, ASinkGetsEveryEventNumberedWithTheSubscribersAndWithoutThem)
{
    std::mutex mutex;
    std::vector<std::pair<std::string, Json::Value>> sunk;
    const uint64_t sink = _hub.AddSink([&](const std::string& topic, const Json::Value& event) {
        std::lock_guard<std::mutex> lock(mutex);
        sunk.emplace_back(topic, event);
    });
    auto count = [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return sunk.size();
    };

    // No subscriber of its own: the sink (the debug.* topics of /api/v1/ws) still gets the events
    PostPause(kFirst, false);
    ASSERT_TRUE(TestWait::For([&] { return count() == 1; }, std::chrono::seconds(2)));
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"a","emulator":")" + std::string(kFirst) + R"("})",
                       inbox.Sender());
    PostStepDone(kFirst, false);
    ASSERT_TRUE(TestWait::For([&] { return count() == 2 && inbox.Events().size() == 1; }, std::chrono::seconds(2)));
    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(sunk[0].first, "debug");
        EXPECT_EQ(sunk[0].second["event"].asString(), "paused");
        EXPECT_EQ(sunk[0].second["seq"].asUInt64(), 1u);
        EXPECT_EQ(sunk[1].second["seq"].asUInt64(), 2u) << "one numbering for the socket and the sink";
        EXPECT_EQ(sunk[1].second["emulator"].asString(), kFirst);
    }
    EXPECT_EQ(inbox.Events()[0]["seq"].asUInt64(), 2u);

    _hub.RemoveSink(sink);
    PostPause(kFirst, false);
    ASSERT_TRUE(TestWait::For([&] { return inbox.Events().size() == 2; }, std::chrono::seconds(2)));
    EXPECT_EQ(count(), 2u) << "a removed sink is not called again";
}

/// NC_DEBUG_EDITED: a tool's edit that went through is `edited` with what it changed; a register is `registers`
TEST_F(DebugEventHub_Test, EditedNamesWhatAToolChanged)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto edits = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString() == "edited")
                found.push_back(event);
        return found;
    };

    Memory* memory = emulator->GetContext()->pMemory;
    EXPECT_TRUE(emulator->EditMemoryFromTool(Emulator::ToolEdit::Of("test memory", "memory", 0x8000, 2), [&] {
        memory->DirectWriteToZ80Memory(0x8000, 0x12);
        memory->DirectWriteToZ80Memory(0x8001, 0x34);
    }).empty());
    ASSERT_TRUE(TestWait::For([&] { return edits().size() == 1; }, std::chrono::seconds(2)));
    std::string refusal;
    ASSERT_TRUE(emulator->SetRegisterFromTool("test register", "HL", 0x1234, &refusal)) << refusal;
    ASSERT_TRUE(TestWait::For([&] { return edits().size() == 2; }, std::chrono::seconds(2)));
    const std::vector<Json::Value> events = edits();
    EXPECT_EQ(events[0]["count"].asUInt(), 1u);
    EXPECT_EQ(events[0]["source"].asString(), "test memory");
    EXPECT_EQ(events[0]["cpu"].asString(), "main");
    EXPECT_EQ(events[0]["what"].asString(), "memory");
    EXPECT_EQ(events[0]["address"].asInt(), 0x8000);
    EXPECT_EQ(events[0]["length"].asUInt(), 2u);
    EXPECT_FALSE(events[0].isMember("page"));
    EXPECT_EQ(events[1]["what"].asString(), "registers");
    EXPECT_FALSE(events[1].isMember("address")) << "a register edit has no address";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A script writing memory byte by byte: one NC_DEBUG_EDITED waits in the message queue at a time, the edits come
/// merged (their count, one range); with nobody taking them (no hub) the queue sees at most ten a second
TEST_F(DebugEventHub_Test, EditsUnderLoadAreMergedAndTheQueueStaysShort)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    std::atomic<int> posted{0};
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    const uint64_t observer =
        messageCenter.AddObserver(NC_DEBUG_EDITED, [&posted](int, Message*) { posted.fetch_add(1); });
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto edits = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString() == "edited")
                found.push_back(event);
        return found;
    };
    auto counted = [&] {
        uint64_t total = 0;
        for (const Json::Value& event : edits())
            total += event["count"].asUInt();
        return total;
    };

    constexpr int kEdits = 20000;
    Memory* memory = emulator->GetContext()->pMemory;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kEdits; i++)
    {
        const uint16_t address = static_cast<uint16_t>(0x8000 + (i % 0x4000));
        ASSERT_TRUE(emulator->EditMemoryFromTool(Emulator::ToolEdit::Of("load", "memory", address, 1), [&] {
            memory->DirectWriteToZ80Memory(address, static_cast<uint8_t>(i));
        }).empty());
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
    ASSERT_TRUE(TestWait::For([&] { return counted() == kEdits; }, std::chrono::seconds(2)));
    const std::vector<Json::Value> events = edits();
    std::cout << "        " << kEdits << " edits in " << elapsed.count() << " us: " << events.size() << " events, "
              << posted.load() << " notifications" << std::endl;
    EXPECT_LT(events.size(), static_cast<size_t>(kEdits / 10)) << "merged, not one event an edit";
    EXPECT_LE(static_cast<int>(events.size()), posted.load()) << "an event per notification at most";
    for (const Json::Value& event : events)
    {
        EXPECT_EQ(event["what"].asString(), "memory");
        EXPECT_GE(event["address"].asInt(), 0x8000);
        EXPECT_LE(event["address"].asInt() + event["length"].asInt(), 0xC000);
    }

    // Nobody takes them: a notification on the first edit, then at most one each 100 ms
    _hub.Stop();
    const int before = posted.load();
    const auto unheardStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kEdits; i++)
        ASSERT_TRUE(emulator->EditMemoryFromTool(Emulator::ToolEdit::Of("load", "memory", 0x8000, 1), [] {}).empty());
    const auto unheard =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - unheardStart);
    ASSERT_TRUE(TestWait::For([&] { return posted.load() > before; }, std::chrono::seconds(2)));
    EXPECT_LE(posted.load() - before, 1 + static_cast<int>(unheard.count() / 100) + 1);
    messageCenter.RemoveObserverById(NC_DEBUG_EDITED, observer);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// NC_RUN_CONTROL_CHANGED: run_control with the change, the surface, the client and who took control back
TEST_F(DebugEventHub_Test, RunControlCarriesTheChange)
{
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s"})", inbox.Sender());
    MessageCenter::DefaultMessageCenter().Post(
        NC_RUN_CONTROL_CHANGED,
        new RunControlChangedPayload(unreal::UUID(std::string(kFirst)), "revoked", "mcp", "agent", "owner-1", "gui"),
        true);
    ASSERT_TRUE(TestWait::For([&] { return inbox.Events().size() == 1; }, std::chrono::seconds(2)));
    const Json::Value event = inbox.Events()[0];
    EXPECT_EQ(event["event"].asString(), "run_control");
    EXPECT_EQ(event["emulator"].asString(), kFirst);
    EXPECT_EQ(event["change"].asString(), "revoked");
    EXPECT_EQ(event["surface"].asString(), "mcp");
    EXPECT_EQ(event["client"].asString(), "agent");
    EXPECT_EQ(event["owner"].asString(), "owner-1");
    EXPECT_EQ(event["by"].asString(), "gui");
}

/// NC_LABEL_CHANGED: labels_changed for the instance whose labels changed; a label manager without one sends none
TEST_F(DebugEventHub_Test, LabelsChangedNamesTheInstance)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","topics":["debug"]})", inbox.Sender());
    MessageCenter::DefaultMessageCenter().Post(NC_LABEL_CHANGED, new LabelsChangedPayload(std::string()), true);
    LabelManager* labels = emulator->GetContext()->pDebugManager->GetLabelManager();
    ASSERT_TRUE(labels->AddLabel("Start", 0x8000, 0, 0));
    auto changes = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString() == "labels_changed")
                found.push_back(event);
        return found;
    };
    ASSERT_TRUE(TestWait::For([&] { return !changes().empty(); }, std::chrono::seconds(2)));
    for (const Json::Value& event : changes())
        EXPECT_EQ(event["emulator"].asString(), emulator->GetId()) << "no event without an instance";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// NC_DEBUG_TARGETS_CHANGED: targets_changed when a card CPU goes (the General Sound switched to the lightweight
/// player, which has none) and comes back; not for the cards the machine was built with, nor while the set stays
TEST_F(DebugEventHub_Test, TargetsChangedListsTheCpusOnAChange)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    SoundManager* soundManager = emulator->GetContext()->pSoundManager;
    ASSERT_TRUE(FitGeneralSoundCard(soundManager, GSTypeKind::NGS));
    Inbox inbox;
    _hub.HandleMessage(&inbox, R"({"op":"subscribe","id":"s","emulator":")" + emulator->GetId() + R"(","topics":["debug"]})",
                       inbox.Sender());
    auto targets = [&inbox] {
        std::vector<Json::Value> found;
        for (const Json::Value& event : inbox.Events())
            if (event["event"].asString() == "targets_changed")
                found.push_back(event);
        return found;
    };
    auto available = [](const Json::Value& event, const std::string& cpu) {
        for (const Json::Value& target : event["targets"])
            if (target["cpu"].asString() == cpu)
                return target["available"].asBool();
        return false;
    };

    ASSERT_TRUE(soundManager->switchGeneralSoundCard(GSTypeKind::LW));
    ASSERT_TRUE(TestWait::For([&] { return targets().size() == 1; }, std::chrono::seconds(2)));
    EXPECT_TRUE(available(targets()[0], "main"));
    EXPECT_FALSE(available(targets()[0], "gs")) << "the lightweight player has no CPU";
    EXPECT_FALSE(available(targets()[0], "ms"));

    ASSERT_TRUE(soundManager->switchGeneralSoundCard(GSTypeKind::NGS));
    ASSERT_TRUE(TestWait::For([&] { return targets().size() == 2; }, std::chrono::seconds(2)));
    EXPECT_TRUE(available(targets()[1], "gs"));

    // The same set again: nothing; an edit after it shows the queue went past
    emulator->GetContext()->pDebugManager->OnGeneralSoundCardCreated();
    ASSERT_TRUE(emulator->EditMemoryFromTool(Emulator::ToolEdit::Of("test", "memory", 0x8000, 1), [] {}).empty());
    ASSERT_TRUE(TestWait::For(
        [&] {
            for (const Json::Value& event : inbox.Events())
                if (event["event"].asString() == "edited")
                    return true;
            return false;
        },
        std::chrono::seconds(2)));
    EXPECT_EQ(targets().size(), 2u);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
