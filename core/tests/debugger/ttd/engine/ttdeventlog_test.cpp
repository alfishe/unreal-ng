/// @file ttdeventlog_test.cpp
/// @brief The engine's event log and payload store (Phase 3, Step 1): order,
/// cursors, roles (a sealed replay has no barriers but v1 records without
/// their data), v1 input records packed losslessly, payloads kept while
/// referenced

#include <gtest/gtest.h>

#include <vector>

#include "debugger/ttd/engine/ttdeventlog.h"
#include "debugger/ttd/engine/ttdpayloadstore.h"
#include "debugger/ttd/ttdinputjournal.h"

using namespace ttd;

TEST(TTDPayloadStore_Test, BytesLiveWhileReferenced)
{
    TTDPayloadStore store;
    const std::vector<uint8_t> bytes = {1, 2, 3};
    const TTDPayloadRef a = store.Store(bytes.data(), bytes.size());
    EXPECT_EQ(store.Bytes(a), bytes);
    store.AddRef(a);   // a checkpoint's network device names it too
    store.Release(a);  // the event is dropped
    EXPECT_EQ(store.Bytes(a), bytes) << "still referenced by the checkpoint";
    store.Release(a);
    EXPECT_TRUE(store.Bytes(a).empty());
    EXPECT_EQ(store.LiveCount(), 0u);
    const TTDPayloadRef b = store.Store(bytes.data(), 2);
    EXPECT_EQ(b.id, a.id) << "a freed id is reused";
}

TEST(TTDEventLog_Test, OrderCursorsAndSeq)
{
    TTDPayloadStore payloads;
    TTDEventLog log(payloads);
    TTDEvent e;
    e.kind = TTDEventKind::TapeControl;
    for (TTDMachineTime t : {10u, 20u, 20u, 35u})
    {
        e.machineTime = t;
        ASSERT_TRUE(log.Append(e));
    }
    EXPECT_EQ(log.At(1).seq, 0u);
    EXPECT_EQ(log.At(2).seq, 1u) << "same instant: recording order";
    e.machineTime = 30;
    const std::vector<uint8_t> reason = {'x'};
    e.payload = payloads.Store(reason.data(), reason.size());
    EXPECT_FALSE(log.Append(e)) << "before the last event";
    EXPECT_EQ(payloads.LiveCount(), 0u) << "a refused event releases its payload";
    EXPECT_EQ(log.CursorAt(0), 0u);
    EXPECT_EQ(log.CursorAt(20), 1u);
    EXPECT_EQ(log.CursorAt(21), 3u);
    EXPECT_EQ(log.CursorAt(36), 4u);
}

TEST(TTDEventLog_Test, ASealedReplayHasNoBarriers_OnlyV1RecordsWithoutData)
{
    TTDPayloadStore payloads;
    TTDEvent ev;
    for (TTDEventKind k : {TTDEventKind::TapeControl, InputEventKind(0) /*Key*/, InputEventKind(13) /*NetEvent*/})
    {
        ev.kind = k;
        EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Input) << static_cast<int>(k);
    }
    ev.kind = TTDEventKind::MediaWrite;
    EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Fact) << "the replay writes again, held from the host";
    ev.kind = TTDEventKind::DebuggerEdit;
    EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Barrier) << "a v1 edit carries no bytes";
    const uint8_t bytes[] = {0x3E, 0x01};
    ev.payload = payloads.Store(bytes, 2);
    EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Barrier) << "a v1 edit's payload is its reason text";
    ev.args[0] = kEditCarriesData;
    EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Input) << "an edit with its bytes replays";
    ev.args[0] = 0;
    payloads.Release(ev.payload);
    ev.payload = {};
    ev.kind = TTDEventKind::HardwareReset;
    EXPECT_EQ(TTDEventLog::RoleOf(ev), TTDEventRole::Barrier) << "v1 files only (D39)";

    TTDEventLog log(payloads);
    ev.kind = TTDEventKind::MediaWrite;
    ev.machineTime = 5;
    log.Append(ev);
    ev.kind = TTDEventKind::TapeControl;
    ev.machineTime = 7;
    log.Append(ev);
    EXPECT_EQ(log.FirstBarrierIn(0, 100), nullptr) << "disk writes and tape commands do not stop a seek";
    ev.kind = TTDEventKind::OtherMarker;
    ev.machineTime = 9;
    log.Append(ev);
    ASSERT_NE(log.FirstBarrierIn(0, 100), nullptr);
    EXPECT_EQ(log.FirstBarrierIn(0, 100)->machineTime, 9u);
    EXPECT_EQ(log.FirstBarrierIn(9, 100), nullptr) << "(from, to]";
}

TEST(TTDEventLog_Test, V1InputAndNetworkRecordsRoundTrip)
{
    TTDInputEvent in;
    in.kind = TTDInputKind::MouseMove;
    in.key = 7;
    in.pressed = true;
    in.dx = -300;
    in.dy = 1234;
    in.buttonMask = 0x5A;
    in.wheelSteps = -3;
    in.value = 0xC9;
    const TTDEvent ev = TTDEventLog::FromInput(4242, in);
    EXPECT_EQ(static_cast<uint16_t>(ev.kind), static_cast<uint16_t>(TTDInputKind::MouseMove)) << "the same number";
    EXPECT_EQ(ev.machineTime, 4242u);
    TTDInputEvent out;
    TTDEventLog::ToInput(ev, out);
    EXPECT_EQ(out.kind, in.kind);
    EXPECT_EQ(out.key, in.key);
    EXPECT_EQ(out.pressed, in.pressed);
    EXPECT_EQ(out.dx, in.dx);
    EXPECT_EQ(out.dy, in.dy);
    EXPECT_EQ(out.buttonMask, in.buttonMask);
    EXPECT_EQ(out.wheelSteps, in.wheelSteps);
    EXPECT_EQ(out.value, in.value);

    TTDNetInput net;
    net.socket = 513;
    net.event = 4;
    net.status = 2;
    net.addr = 0xC0A80001u;
    net.port = 8080;
    TTDEvent nev;
    TTDEventLog::PackNet(net, nev);
    TTDNetInput back;
    TTDEventLog::UnpackNet(nev, back);
    EXPECT_EQ(back.socket, net.socket);
    EXPECT_EQ(back.event, net.event);
    EXPECT_EQ(back.status, net.status);
    EXPECT_EQ(back.addr, net.addr);
    EXPECT_EQ(back.port, net.port);
}
