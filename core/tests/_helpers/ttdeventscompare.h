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

}  // namespace ttdtest
