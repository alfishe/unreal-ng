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

/// endregion </BreakpointTriggeredPayload unit tests>

/// region <MessageCenter round-trip>

namespace
{
/// A tiny legacy observer that only knows about SimpleNumberPayload.
/// Used to prove existing observers keep working unchanged.
struct LegacyStateObserver : public Observer
{
    std::atomic<uint32_t> lastState{0};
    std::atomic<int> hits{0};

    // Observer base has no virtual method; signature must match ObserverCallbackMethod.
    void onEvent(int /*id*/, Message* message)
    {
        if (auto* p = dynamic_cast<SimpleNumberPayload*>(message->obj))
        {
            lastState.store(p->_payloadNumber);
            hits.fetch_add(1);
        }
    }
};

/// A new-style observer that uses the instance UUID for filtering.
struct InstanceFilteringObserver : public Observer
{
    UUID watchId;
    std::atomic<uint32_t> lastState{0};
    std::atomic<int> hits{0};
    std::atomic<int> filteredMisses{0};

    explicit InstanceFilteringObserver(const UUID& id) : watchId(id) {}

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
        else
        {
            filteredMisses.fetch_add(1);
        }
    }
};
} // namespace

TEST(InstanceTaggedPayloads_Test, LegacyObserverStillReceivesStateViaPayloadNumber)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    LegacyStateObserver obs;
    Observer* obsPtr = &obs;

    ObserverCallbackMethod cb =
        static_cast<ObserverCallbackMethod>(&LegacyStateObserver::onEvent);
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    UUID id = UUID::Generate();
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(id, StateRun));
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(id, StatePaused));

    // MessageCenter dispatches async; let it drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    EXPECT_EQ(obs.hits.load(), 2);
    EXPECT_EQ(obs.lastState.load(), static_cast<uint32_t>(StatePaused));
}

TEST(InstanceTaggedPayloads_Test, InstanceFilteringObserverIgnoresOtherInstances)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    const UUID mine = UUID::Generate();
    const UUID other = UUID::Generate();
    ASSERT_FALSE(mine == other);

    InstanceFilteringObserver obs(mine);
    Observer* obsPtr = &obs;
    ObserverCallbackMethod cb =
        static_cast<ObserverCallbackMethod>(&InstanceFilteringObserver::onEvent);
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

    // Event from a different instance — must be ignored.
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(other, StateRun));
    // Event from our instance — must be received.
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(mine, StatePaused));

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obsPtr, cb);

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

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    mc.RemoveObserver(NC_EXECUTION_BREAKPOINT, obsPtr, cb);

    EXPECT_EQ(obs.hits.load(), 1);
    EXPECT_EQ(obs.lastBpId.load(), 7u);
    EXPECT_EQ(obs.lastAddr.load(), 0x4242u);
}

/// endregion </MessageCenter round-trip>
