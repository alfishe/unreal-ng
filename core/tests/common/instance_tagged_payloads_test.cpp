// Tests for instance-tagged notification payloads (Sprint 0, Item 0.1).
// GDB TDD §6.3 — prerequisite for per-instance observers.
//
// Covers:
//   * EmulatorStateChangePayload — subclass of SimpleNumberPayload carrying
//     the new state in _payloadNumber (legacy observers keep working) and the
//     instance UUID in emulatorId (new observers use it for filtering).
//   * BreakpointTriggeredPayload — subclass of SimpleNumberPayload carrying
//     the breakpoint ID in _payloadNumber and the instance UUID + address in
//     the new fields.
//
// Also includes an end-to-end post/observe round-trip through MessageCenter
// to confirm that the instance UUID survives the dispatch and that legacy
// observers reading just _payloadNumber are unaffected.

#include "pch.h"

#include <emulator/emulator.h>  // EmulatorStateEnum (StateRun, StatePaused, ...)
#include <emulator/notifications.h>

#include "_helpers/testwaithelper.h"
#include <emulator/platform.h>
#include <3rdparty/message-center/messagecenter.h>

#include <atomic>
#include <chrono>
#include <thread>

/// region <EmulatorStateChangePayload unit tests>

TEST(InstanceTaggedPayloads_Test, StateChangePayload_IsASimpleNumberPayload)
{
    EmulatorStateChangePayload p(UUID::Generate(), StateRun);

    // Legacy observers dynamic_cast to SimpleNumberPayload — must succeed.
    auto* legacy = dynamic_cast<SimpleNumberPayload*>(&p);
    ASSERT_NE(legacy, nullptr);
    EXPECT_EQ(legacy->_payloadNumber, static_cast<uint32_t>(StateRun));
}

TEST(InstanceTaggedPayloads_Test, StateChangePayload_CarriesInstanceUuidAndState)
{
    const UUID id = UUID::Generate();
    EmulatorStateChangePayload p(id, StatePaused);

    EXPECT_EQ(p._payloadNumber, static_cast<uint32_t>(StatePaused));
    EXPECT_EQ(p.emulatorId, id);
}

TEST(InstanceTaggedPayloads_Test, StateChangePayload_AcceptsStringUuid)
{
    const UUID id = UUID::Generate();
    const std::string idStr = id.toString();

    EmulatorStateChangePayload p(idStr, StateResumed);
    EXPECT_EQ(p.emulatorId, id);
    EXPECT_EQ(p._payloadNumber, static_cast<uint32_t>(StateResumed));
}

TEST(InstanceTaggedPayloads_Test, StateChangePayload_EmptyStringUuidBecomesNil)
{
    EmulatorStateChangePayload p(std::string{}, StateStopped);
    UUID nil;
    EXPECT_EQ(p.emulatorId, nil);
}

/// endregion </EmulatorStateChangePayload unit tests>

/// region <BreakpointTriggeredPayload unit tests>

TEST(InstanceTaggedPayloads_Test, BreakpointPayload_IsASimpleNumberPayload)
{
    BreakpointTriggeredPayload p(UUID::Generate(), 42, 0x1234);

    auto* legacy = dynamic_cast<SimpleNumberPayload*>(&p);
    ASSERT_NE(legacy, nullptr);
    EXPECT_EQ(legacy->_payloadNumber, 42u);
}

TEST(InstanceTaggedPayloads_Test, BreakpointPayload_CarriesInstanceUuidIdAndAddress)
{
    const UUID id = UUID::Generate();
    BreakpointTriggeredPayload p(id, 17, 0xABCD);

    EXPECT_EQ(p._payloadNumber, 17u);
    EXPECT_EQ(p.address, 0xABCDu);
    EXPECT_EQ(p.emulatorId, id);
}

TEST(InstanceTaggedPayloads_Test, BreakpointPayload_AcceptsStringUuid)
{
    const UUID id = UUID::Generate();
    BreakpointTriggeredPayload p(id.toString(), 99, 0x0001);

    EXPECT_EQ(p.emulatorId, id);
    EXPECT_EQ(p.address, 0x0001u);
}

TEST(InstanceTaggedPayloads_Test, BreakpointPayload_CarriesHiddenFlag)
{
    const UUID id = UUID::Generate();
    BreakpointTriggeredPayload p1(id, 10, 0x1000, false);
    EXPECT_FALSE(p1.hidden);

    BreakpointTriggeredPayload p2(id, 11, 0x2000, true);
    EXPECT_TRUE(p2.hidden);

    BreakpointTriggeredPayload p3(id.toString(), 12, 0x3000, true);
    EXPECT_TRUE(p3.hidden);
}

/// endregion </BreakpointTriggeredPayload unit tests>

/// region <MessageCenter round-trip>

namespace
{
/// A tiny legacy observer that only knows about SimpleNumberPayload.
/// Used to prove existing observers keep working unchanged.
///
/// The MessageCenter is process-wide: an emulator left by a neighbouring test
/// in the same shard posts its own state changes on the same topic. The test
/// counts only the events it posted itself (`testId`); the state is still read
/// the legacy way, through SimpleNumberPayload::_payloadNumber
struct LegacyStateObserver : public Observer
{
    UUID testId;
    std::atomic<uint32_t> lastState{0};
    std::atomic<int> hits{0};

    explicit LegacyStateObserver(const UUID& id) : testId(id) {}

    // Observer base has no virtual method; signature must match ObserverCallbackMethod.
    void onEvent(int /*id*/, Message* message)
    {
        auto* tagged = dynamic_cast<EmulatorStateChangePayload*>(message->obj);
        if (!tagged || !(tagged->emulatorId == testId))
            return;  // another test's emulator
        if (auto* p = dynamic_cast<SimpleNumberPayload*>(message->obj))
        {
            lastState.store(p->_payloadNumber);
            hits.fetch_add(1);
        }
    }
};

/// A new-style observer that uses the instance UUID for filtering.
/// `filteredMisses` counts only the other instance this test posts for, not
/// the emulators of neighbouring tests (see LegacyStateObserver)
struct InstanceFilteringObserver : public Observer
{
    UUID watchId;
    UUID otherId;
    std::atomic<uint32_t> lastState{0};
    std::atomic<int> hits{0};
    std::atomic<int> filteredMisses{0};

    InstanceFilteringObserver(const UUID& id, const UUID& other) : watchId(id), otherId(other) {}

    // Observer base has no virtual method; signature must match ObserverCallbackMethod.
    void onEvent(int /*id*/, Message* message)
    {
        auto* p = dynamic_cast<EmulatorStateChangePayload*>(message->obj);
        if (!p) return;
        if (p->emulatorId == watchId)
        {
            lastState.store(p->_payloadNumber);
            hits.fetch_add(1);
        }
        else if (p->emulatorId == otherId)
        {
            filteredMisses.fetch_add(1);
        }
    }
};
} // namespace

TEST(InstanceTaggedPayloads_Test, LegacyObserverStillReceivesStateViaPayloadNumber)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    const UUID id = UUID::Generate();
    LegacyStateObserver obs(id);
    Observer* obsPtr = &obs;

    ObserverCallbackMethod cb =
        static_cast<ObserverCallbackMethod>(&LegacyStateObserver::onEvent);
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    // A neighbouring test's emulator on the same topic (the flake this test had)
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(UUID::Generate(), StateStopped));
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(id, StateRun));
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(id, StatePaused));

    // MessageCenter dispatches async; wait for both deliveries rather than
    // guessing a drain interval.
    const bool delivered = TestWait::For([&obs] { return obs.hits.load() >= 2; });

    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    ASSERT_TRUE(delivered) << "both state-change events must reach the observer";
    EXPECT_EQ(obs.hits.load(), 2);
    EXPECT_EQ(obs.lastState.load(), static_cast<uint32_t>(StatePaused));
}

TEST(InstanceTaggedPayloads_Test, InstanceFilteringObserverIgnoresOtherInstances)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    const UUID mine = UUID::Generate();
    const UUID other = UUID::Generate();
    ASSERT_FALSE(mine == other);

    InstanceFilteringObserver obs(mine, other);
    Observer* obsPtr = &obs;
    ObserverCallbackMethod cb =
        static_cast<ObserverCallbackMethod>(&InstanceFilteringObserver::onEvent);
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    // A neighbouring test's emulator - neither ours nor the "other" one
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(UUID::Generate(), StateStopped));
    // Event from a different instance — must be ignored.
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(other, StateRun));
    // Event from our instance — must be received.
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(mine, StatePaused));

    // Both events must be dispatched: one accepted, one filtered out.
    const bool delivered =
        TestWait::For([&obs] { return obs.hits.load() >= 1 && obs.filteredMisses.load() >= 1; });
    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    ASSERT_TRUE(delivered) << "both events must be dispatched before asserting";
    EXPECT_EQ(obs.hits.load(), 1) << "Only events from our instance must be accepted";
    EXPECT_EQ(obs.filteredMisses.load(), 1) << "Other-instance event must be filtered";
    EXPECT_EQ(obs.lastState.load(), static_cast<uint32_t>(StatePaused));
}

TEST(InstanceTaggedPayloads_Test, BreakpointPayloadRoundTripsThroughMessageCenter)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    struct BPObserver : public Observer
    {
        UUID watchId;
        std::atomic<uint32_t> lastBpId{0};
        std::atomic<uint16_t> lastAddr{0};
        std::atomic<int> hits{0};
        explicit BPObserver(const UUID& id) : watchId(id) {}
        // Observer base has no virtual method; signature must match ObserverCallbackMethod.
        void onEvent(int, Message* message)
        {
            auto* p = dynamic_cast<BreakpointTriggeredPayload*>(message->obj);
            if (p && p->emulatorId == watchId)
            {
                lastBpId.store(p->_payloadNumber);
                lastAddr.store(p->address);
                hits.fetch_add(1);
            }
        }
    };

    const UUID mine = UUID::Generate();
    BPObserver obs(mine);
    Observer* obsPtr = &obs;
    ObserverCallbackMethod cb =
        static_cast<ObserverCallbackMethod>(&BPObserver::onEvent);
    mc.AddObserver(NC_EXECUTION_BREAKPOINT, obsPtr, cb);

    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(mine, 7, 0x4242));

    const bool delivered = TestWait::For([&obs] { return obs.hits.load() >= 1; });
    mc.RemoveObserver(NC_EXECUTION_BREAKPOINT, obsPtr, cb);

    ASSERT_TRUE(delivered) << "breakpoint payload must reach the observer";
    EXPECT_EQ(obs.hits.load(), 1);
    EXPECT_EQ(obs.lastBpId.load(), 7u);
    EXPECT_EQ(obs.lastAddr.load(), 0x4242u);
}

/// endregion </MessageCenter round-trip>
