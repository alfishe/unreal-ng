// libsam2695 tests - serial line receiver and MIDI parser.
#include "midi/parser.h"
#include "midi/uart.h"
#include "testfw.h"

#include <cmath>
#include <utility>
#include <vector>

using namespace sam2695;

namespace
{

constexpr uint64_t kHost = 3500000; // Z80 T-states: one MIDI bit is exactly 112 host ticks
constexpr double kBit = static_cast<double>(kHost) / kMidiBaud;

struct Line
{
    Uart uart;
    std::vector<std::pair<uint64_t, uint8_t>> got;

    Line()
    {
        uart.Configure(kHost);
        uart.Reset();
    }
    auto Sink()
    {
        return [this](uint64_t t, uint8_t b) { got.emplace_back(t, b); };
    }
    void Level(double t, bool level) { uart.SetLevel(static_cast<uint64_t>(std::llround(t)), level, Sink()); }
    // One 8N1 frame from t0 with the given bit time; returns the time the stop bit ends.
    double Send(double t0, uint8_t byte, double bit = kBit, bool stop = true)
    {
        Level(t0, false);
        for (int i = 0; i < 8; i++)
            Level(t0 + (i + 1) * bit, ((byte >> i) & 1) != 0);
        Level(t0 + 9 * bit, stop);
        return t0 + 10 * bit;
    }
    void Finish(double t) { uart.AdvanceTo(static_cast<uint64_t>(std::llround(t)), Sink()); }
};

struct Collector
{
    std::vector<MidiMessage> msgs;
    std::vector<std::vector<uint8_t>> sysEx;
    MidiParser parser;

    void Feed(std::initializer_list<int> bytes)
    {
        for (int b : bytes)
            parser.Feed(static_cast<uint8_t>(b), [this](const MidiMessage& m) {
                msgs.push_back(m);
                if (m.status == 0xF0)
                    sysEx.emplace_back(m.sysEx, m.sysEx + m.sysExLength);
            });
    }
};

} // namespace

TEST(Uart, ByteTiming)
{
    // tdd §1 worked example: Note On 90 3C 64; each byte arrives at its stop bit's middle
    Line line;
    double t = 7000.0; // a whole receive tick (7 host ticks per 1/16 bit)
    for (uint8_t b : {0x90, 0x3C, 0x64})
        t = line.Send(t, b);
    line.Finish(t + 1000);
    CHECK_EQ_I(line.got.size(), 3);
    if (line.got.size() == 3)
    {
        CHECK_EQ_I(line.got[0].second, 0x90);
        CHECK_EQ_I(line.got[1].second, 0x3C);
        CHECK_EQ_I(line.got[2].second, 0x64);
        CHECK_EQ_I(line.got[0].first, 7000 + 1064);           // 9.5 bits of 112 ticks
        CHECK_EQ_I(line.got[2].first - 7000, 2 * 1120 + 1064); // 960 us after T0, less half a stop bit
    }
    CHECK_EQ_I(line.uart.FramingErrors(), 0);
}

TEST(Uart, FramingError)
{
    Line line;
    double t = line.Send(1000.0, 0x55, kBit, false); // stop bit low
    line.Level(t + 3 * kBit, true);                  // line back to idle
    t = line.Send(t + 5 * kBit, 0xA5);
    line.Finish(t + 1000);
    CHECK_EQ_I(line.uart.FramingErrors(), 1);
    CHECK_EQ_I(line.got.size(), 1);
    if (!line.got.empty())
        CHECK_EQ_I(line.got[0].second, 0xA5);
}

TEST(Uart, FalseStart)
{
    Line line;
    line.Level(1000.0, false);
    line.Level(1000.0 + kBit / 4, true); // a glitch of a quarter bit
    line.Finish(1000.0 + 20 * kBit);
    CHECK_EQ_I(line.got.size(), 0);
    CHECK_EQ_I(line.uart.FalseStarts(), 1);
    CHECK_EQ_I(line.uart.FramingErrors(), 0);
}

TEST(Uart, BaudTolerance)
{
    // +-2 % decodes every byte back to back; +-6 % does not (the receiver's limit is +-4.6 %)
    for (double factor : {0.98, 1.02, 0.94, 1.06})
    {
        Line line;
        double t = 3500.0;
        for (int b = 0; b < 256; b++)
            t = line.Send(t, static_cast<uint8_t>(b), kBit * factor);
        line.Finish(t + 20 * kBit);
        int correct = 0;
        for (size_t i = 0; i < line.got.size() && i < 256; i++)
            correct += line.got[i].second == i ? 1 : 0;
        if (std::fabs(factor - 1.0) < 0.03)
        {
            CHECK_EQ_I(correct, 256);
            CHECK_EQ_I(line.uart.FramingErrors(), 0);
        }
        else
            CHECK(correct < 200);
    }
}

TEST(Uart, IdleGapsAndLevelHolds)
{
    // bytes separated by idle time of any length, sent at arbitrary host-tick phases
    Line line;
    double t = 123.0;
    const uint8_t bytes[] = {0x00, 0xFF, 0x80, 0x01, 0x7F};
    for (uint8_t b : bytes)
    {
        t = line.Send(t, b);
        t += 37.0 + 1000.0 * b; // idle gap
    }
    line.Finish(t + 1000);
    CHECK_EQ_I(line.got.size(), 5);
    for (size_t i = 0; i < line.got.size() && i < 5; i++)
        CHECK_EQ_I(line.got[i].second, bytes[i]);
}

TEST(Parser, RunningStatus)
{
    Collector c;
    c.Feed({0x90, 0x3C, 0x64, 0x3E, 0x64, 0x3C, 0x00});
    CHECK_EQ_I(c.msgs.size(), 3);
    if (c.msgs.size() == 3)
    {
        CHECK_EQ_I(c.msgs[1].status, 0x90);
        CHECK_EQ_I(c.msgs[1].data1, 0x3E);
        CHECK_EQ_I(c.msgs[2].data2, 0);
    }
    // program change: one data byte, running status too
    Collector p;
    p.Feed({0xC1, 5, 6});
    CHECK_EQ_I(p.msgs.size(), 2);
}

TEST(Parser, RealtimeInsideMessage)
{
    Collector c;
    c.Feed({0x90, 0xF8, 0x3C, 0xFE, 0x64, 0xFA});
    CHECK_EQ_I(c.msgs.size(), 4);
    if (c.msgs.size() == 4)
    {
        CHECK_EQ_I(c.msgs[0].status, 0xF8);
        CHECK_EQ_I(c.msgs[1].status, 0xFE);
        CHECK_EQ_I(c.msgs[2].status, 0x90);
        CHECK_EQ_I(c.msgs[2].data1, 0x3C);
        CHECK_EQ_I(c.msgs[2].data2, 0x64);
        CHECK_EQ_I(c.msgs[3].status, 0xFA);
    }
    // realtime inside SysEx does not end it
    Collector s;
    s.Feed({0xF0, 0x7E, 0xF8, 0x7F, 0x09, 0x01, 0xF7});
    CHECK_EQ_I(s.sysEx.size(), 1);
    if (!s.sysEx.empty())
        CHECK_EQ_I(s.sysEx[0].size(), 4);
}

TEST(Parser, SysExOverflow)
{
    Collector c;
    c.parser.Feed(0xF0, [](const MidiMessage&) {});
    c.Feed({0x41});
    for (size_t i = 0; i < kSysExCapacity + 50; i++)
        c.Feed({0x10});
    c.Feed({0xF7, 0x90, 0x40, 0x40});
    CHECK_EQ_I(c.sysEx.size(), 0);
    CHECK_EQ_I(c.parser.SysExOverflows(), 1);
    CHECK_EQ_I(c.msgs.size(), 1); // the next message is intact
    // a message exactly at the capacity is kept
    Collector e;
    e.Feed({0xF0});
    for (size_t i = 0; i < kSysExCapacity; i++)
        e.Feed({0x01});
    e.Feed({0xF7});
    CHECK_EQ_I(e.sysEx.size(), 1);
}

TEST(Parser, SysExEndedByStatus)
{
    Collector c;
    c.Feed({0xF0, 0x7E, 0x7F, 0x09, 0x01, 0x90, 0x40, 0x7F});
    CHECK_EQ_I(c.sysEx.size(), 1);
    CHECK_EQ_I(c.msgs.size(), 2);
}

TEST(Parser, SystemCommonCancelsRunningStatus)
{
    Collector c;
    c.Feed({0x90, 0x40, 0x40, 0xF3, 0x05, 0x41, 0x41, 0xF6, 0x42});
    // 90 note, F3 song select, data bytes without status are ignored, F6 tune request
    CHECK_EQ_I(c.msgs.size(), 3);
    if (c.msgs.size() == 3)
    {
        CHECK_EQ_I(c.msgs[1].status, 0xF3);
        CHECK_EQ_I(c.msgs[1].data1, 5);
        CHECK_EQ_I(c.msgs[2].status, 0xF6);
    }
}

TEST(Parser, DataWithoutStatusIgnored)
{
    Collector c;
    c.Feed({0x40, 0x40, 0xF4, 0x10, 0xB0, 0x07, 0x64});
    CHECK_EQ_I(c.msgs.size(), 1);
}
