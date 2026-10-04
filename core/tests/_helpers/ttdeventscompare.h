#pragma once

/// @file ttdeventscompare.h
/// @brief Test helper: the engine's event log holds exactly v1's input journal
/// (network records and received bytes included) and external events, each at
/// its time (frame start + offset), kind numbers kept (Phase 3, Step 1)

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"

namespace ttdtest
{

/// @p frameStart: v1's start of a frame in machine time (FrameSpan × frame in the file feeder)
template <typename FrameStart>
inline void ExpectEventsEqualV1(const ttd::TimeTravelEngine& engine, const ttd::TTDInputJournal& input,
                                const ttd::TTDExternalEventJournal& external, size_t inputFrom, size_t externalFrom,
                                FrameStart frameStart)
{
    std::vector<const ttd::TTDEvent*> inputs, markers;
    for (const ttd::TTDEvent& e : engine.Events().Events())
        (ttd::IsInputKind(e.kind) ? inputs : markers).push_back(&e);
    ASSERT_EQ(inputs.size(), input.Size() - inputFrom) << "every input event";
    const std::vector<ttd::TTDExternalEvent> v1Markers = external.SnapshotEvents();
    ASSERT_EQ(markers.size(), v1Markers.size() - externalFrom) << "every marker";

    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const ttd::TTDInputEvent& want = input.Events()[inputFrom + i];
        const ttd::TTDEvent& got = *inputs[i];
        ASSERT_EQ(got.machineTime, frameStart(want.time.frame) + want.time.tInFrame) << "input " << i;
        ttd::TTDInputEvent back;
        ttd::TTDEventLog::ToInput(got, back);
        ASSERT_EQ(back.kind, want.kind) << "input " << i;
        ASSERT_EQ(back.key, want.key);
        ASSERT_EQ(back.pressed, want.pressed);
        ASSERT_EQ(back.dx, want.dx);
        ASSERT_EQ(back.dy, want.dy);
        ASSERT_EQ(back.buttonMask, want.buttonMask);
        ASSERT_EQ(back.wheelSteps, want.wheelSteps);
        ASSERT_EQ(back.value, want.value);
        if (want.kind == ttd::TTDInputKind::NetEvent)
        {
            const ttd::TTDNetInput* net = input.NetOf(want);
            ASSERT_NE(net, nullptr);
            ttd::TTDNetInput gotNet;
            ttd::TTDEventLog::UnpackNet(got, gotNet);
            ASSERT_EQ(gotNet.socket, net->socket);
            ASSERT_EQ(gotNet.event, net->event);
            ASSERT_EQ(gotNet.status, net->status);
            ASSERT_EQ(gotNet.addr, net->addr);
            ASSERT_EQ(gotNet.port, net->port);
            const std::vector<uint8_t>& bytes = engine.Payloads().Bytes(got.payload);
            ASSERT_EQ(bytes.size(), net->payloadLength);
            if (net->payloadLength)
                ASSERT_EQ(std::memcmp(bytes.data(), input.PayloadOf(*net), net->payloadLength), 0) << "input " << i;
        }
    }
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const ttd::TTDExternalEvent& want = v1Markers[externalFrom + i];
        const ttd::TTDEvent& got = *markers[i];
        ASSERT_EQ(got.machineTime, frameStart(want.time.frame) + want.time.tInFrame) << "marker " << i;
        ASSERT_EQ(static_cast<uint16_t>(got.kind), 0x0100 + static_cast<uint16_t>(want.kind));
        const std::vector<uint8_t>& reason = engine.Payloads().Bytes(got.payload);
        ASSERT_EQ(std::string(reason.begin(), reason.end()), std::string(want.reason)) << "marker " << i;
    }
}

/// The engine's bus journals hold v1's port journals record for record, and
/// every engine checkpoint's cursors equal v1's checkpoint of the same index
/// (the engine session began with v1's)
inline void ExpectBusEqualV1(const ttd::TimeTravelEngine& engine, const ttd::TimeTravelManager& v1)
{
    const ttd::TTDPortJournal* v1Journals[2] = {&v1.GetPortReadJournal(), &v1.GetPortWriteJournal()};
    const ttd::TTDPortJournal* engineJournals[2] = {&engine.BusReads(), &engine.BusWrites()};
    for (int j = 0; j < 2; ++j)
    {
        const uint64_t first = v1Journals[j]->FirstIndex();
        ASSERT_EQ(engineJournals[j]->Size(), v1Journals[j]->Size() - first) << (j ? "OUT" : "IN") << " records";
        ttd::TTDPortRecord a, b;
        for (uint64_t i = 0; i < engineJournals[j]->Size(); ++i)
        {
            ASSERT_TRUE(engineJournals[j]->Get(i, a));
            ASSERT_TRUE(v1Journals[j]->Get(first + i, b));
            ASSERT_TRUE(a.SameAccess(b) && a.value == b.value) << (j ? "OUT" : "IN") << " record " << i;
        }
    }
    ASSERT_EQ(engine.CheckpointCount(), v1.GetCheckpointCount());
    for (size_t i = 0; i < engine.CheckpointCount(); ++i)
    {
        ASSERT_EQ(engine.Checkpoint(i)->busReadCursor, v1.GetCheckpoint(i)->portReadCursor) << "checkpoint " << i;
        ASSERT_EQ(engine.Checkpoint(i)->busWriteCursor, v1.GetCheckpoint(i)->portWriteCursor) << "checkpoint " << i;
    }
}

}  // namespace ttdtest
