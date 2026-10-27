// The bus analyzer (docs/inprogress/2026-10-11-bus-analyzer): the trace and its queries, the taps in the SPI masters,
// the SD card's protocol events, checks and timing profiles, the reports every automation plane renders, the export.
// Source: D (design). The card protocol is SdCardSpi's; the TS-Conf SPI DMA is the existing model's.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "../machines/tsconf/tsconffixture.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/zcsdtesthelper.h"
#include "emulator/bus/busexport.h"
#include "emulator/bus/busmanager.h"
#include "emulator/bus/bustap.h"
#include "emulator/bus/bustrace.h"
#include "emulator/bus/spitapdevice.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/io/divmmc/divmmcpaging.h"
#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ata/ataregisters.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/chips/neogs/neogsspi.h"
#include "emulator/state/devicestate.h"

using zcsdtest::PatternDisk;
using zcsdtest::SdCommand;
using zcsdtest::SdInit;

namespace
{
BusTraceEvent At(uint32_t frame, uint32_t t, BusEventKind kind = BusEventKind::SpiByte, uint8_t slot = 0, uint32_t epoch = 0)
{
    BusTraceEvent e;
    e.frame = frame;
    e.t = t;
    e.kind = kind;
    e.slot = slot;
    e.epoch = epoch;
    return e;
}

/// Count of events of a kind
size_t CountKind(const std::vector<BusTraceEvent>& events, BusEventKind kind)
{
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const BusTraceEvent& e) { return e.kind == kind; }));
}

size_t CountRule(const BusTap& tap, BusRule rule)
{
    BusFindingQuery q;
    q.limit = 0;
    q.rules = {rule};
    return tap.Findings(q).size();
}

BusEventQuery All()
{
    BusEventQuery q;
    q.limit = 0;
    return q;
}

/// SD over a bare card
uint8_t X(SdCardSpi& card, uint8_t mosi) { return card.exchange(mosi); }

uint8_t Cmd(SdCardSpi& card, uint8_t index, uint32_t arg, uint8_t crc = 0xFF)
{
    X(card, static_cast<uint8_t>(0x40 | index));
    for (int shift = 24; shift >= 0; shift -= 8)
        X(card, static_cast<uint8_t>(arg >> shift));
    X(card, crc);
    for (int i = 0; i < 16; i++)
        if (uint8_t r = X(card, 0xFF); r != 0xFF)
            return r;
    return 0xFF;
}

/// CMD0, CMD8, ACMD41 until ready; returns the polls ACMD41 needed
int Init(SdCardSpi& card, int maxPolls = 20)
{
    if (Cmd(card, 0, 0, 0x95) != 0x01)
        return -1;
    Cmd(card, 8, 0x1AA, 0x87);
    for (int polls = 1; polls <= maxPolls; polls++)
    {
        Cmd(card, 55, 0);
        if (Cmd(card, 41, 0x40000000) == 0x00)
            return polls;
    }
    return -1;
}

/// Bytes of #FF before the data token
int ToToken(SdCardSpi& card, int max)
{
    for (int i = 0; i < max; i++)
        if (X(card, 0xFF) == 0xFE)
            return i;
    return -1;
}

/// CMD24 + the block; returns the data response (low 5 bits)
uint8_t WriteBlock(SdCardSpi& card, uint32_t block)
{
    if (Cmd(card, 24, block) != 0x00)
        return 0xFF;
    X(card, 0xFF);
    X(card, 0xFE);
    for (int i = 0; i < 512 + 2; i++)
        X(card, 0x55);
    for (int i = 0; i < 16; i++)
        if (uint8_t r = X(card, 0xFF); r != 0xFF)
            return static_cast<uint8_t>(r & 0x1F);
    return 0xFF;
}

/// Bytes of busy (#00) until the card lets the line go
int BusyBytes(SdCardSpi& card, int max)
{
    for (int i = 0; i < max; i++)
        if (X(card, 0xFF) != 0x00)
            return i;
    return -1;
}

struct CardOnTap
{
    BusTap tap{"test.spi", BusProtocol::Spi, "test"};
    SdCardSpi card;
    CardOnTap(uint64_t sectors = 64)
    {
        card.insert(std::make_unique<MemoryDisk>(sectors), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
        card.SetBusTap(&tap, 0);
        tap.Control(1, 1, false);
        card.select(true);
    }
};
}  // namespace

/// region <Trace and queries>

TEST(BusTrace_Test, RingKeepsTheNewestAndSeqNeverGoesBack)
{
    BusTrace trace;
    trace.EnableEvents(3);
    for (uint32_t t = 1; t <= 5; t++)
        trace.Append(At(1, t));
    EXPECT_EQ(trace.Size(), 3u);
    EXPECT_EQ(trace.Evicted(), 2u);
    const auto all = trace.Events(All());
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all.front().seq, 3u) << "oldest first";
    trace.Clear();
    EXPECT_EQ(trace.Size(), 0u);
    EXPECT_EQ(trace.LastSeq(), 5u);
    trace.DisableEvents();
    EXPECT_FALSE(trace.EventsOn());
    EXPECT_EQ(trace.Append(At(2, 0)), 5u) << "off: nothing is appended";
}

TEST(BusTrace_Test, GoingBackInTimeDropsTheFutureButANewEpochDoesNot)
{
    BusTrace trace;
    trace.EnableEvents(16);
    trace.Append(At(10, 100));
    trace.Append(At(10, 200));
    trace.Append(At(11, 50));
    trace.Append(At(10, 150));  // a TTD seek to frame 10 T 150, then live
    auto events = trace.Events(All());
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].t, 100u);
    EXPECT_EQ(events[1].t, 150u);
    EXPECT_EQ(trace.Rewound(), 2u);
    trace.Append(At(0, 10, BusEventKind::SpiByte, 0, 1));  // a reset: frame 0 again, next epoch
    EXPECT_EQ(trace.Events(All()).size(), 3u) << "an earlier frame in a new epoch is not the past";
}

TEST(BusTrace_Test, QueriesFilterKindsSlotSeqFramesAndLimit)
{
    BusTrace trace;
    trace.EnableEvents(16);
    trace.Append(At(1, 0, BusEventKind::SpiSelect, 0));
    trace.Append(At(2, 0, BusEventKind::SpiByte, 0));
    trace.Append(At(3, 0, BusEventKind::SdCommand, 1));
    trace.Append(At(4, 0, BusEventKind::SdReadBlock, 1));
    BusEventQuery q;
    std::string error;
    ASSERT_TRUE(BusEventQueryFromStrings("sd", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u);
    ASSERT_TRUE(BusEventQueryFromStrings("spi", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u);
    ASSERT_TRUE(BusEventQueryFromStrings("command,byte", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u);
    ASSERT_TRUE(BusEventQueryFromStrings("", "1", "", "", "", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u);
    ASSERT_TRUE(BusEventQueryFromStrings("", "", "2", "", "", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u) << "since seq 2";
    ASSERT_TRUE(BusEventQueryFromStrings("", "", "", "2", "3", "", q, error)) << error;
    EXPECT_EQ(trace.Events(q).size(), 2u);
    ASSERT_TRUE(BusEventQueryFromStrings("", "", "", "", "", "1", q, error)) << error;
    ASSERT_EQ(trace.Events(q).size(), 1u);
    EXPECT_EQ(trace.Events(q)[0].kind, BusEventKind::SdReadBlock) << "limit keeps the newest";
    EXPECT_FALSE(BusEventQueryFromStrings("jump", "", "", "", "", "", q, error));
    EXPECT_NE(error.find("spi, sd"), std::string::npos) << "the error lists the kinds: " << error;
    EXPECT_FALSE(BusEventQueryFromStrings("", "x", "", "", "", "", q, error));
    BusFindingQuery fq;
    EXPECT_TRUE(BusFindingQueryFromStrings("dma_collision,early_access", "", "", fq, error)) << error;
    EXPECT_EQ(fq.rules.size(), 2u);
    EXPECT_FALSE(BusFindingQueryFromStrings("races", "", "", fq, error));
}

/// endregion

/// region <Tap and manager>

TEST(BusTap_Test, InactiveUntilControlledAndTheRingsComeAndGo)
{
    BusTap tap("x.spi", BusProtocol::Spi, "x");
    EXPECT_FALSE(tap.Active());
    tap.SpiByte(0, 1, 2, false);
    EXPECT_EQ(tap.Status().capacity, 0u) << "no ring while off";
    tap.Control(1, -1, false, 8);
    EXPECT_TRUE(tap.Active());
    EXPECT_FALSE(tap.Checking());
    tap.SpiByte(0, 0x40, 0xFF, false);
    tap.Finding(BusRule::EarlyAccess, 0);
    BusTapStatus st = tap.Status();
    EXPECT_EQ(st.size, 1u);
    EXPECT_EQ(st.capacity, 8u);
    EXPECT_EQ(st.findings, 0u) << "checks off: no findings";
    tap.Control(-1, 1, false);
    tap.SpiEarly(0, true, 0x12);
    st = tap.Status();
    EXPECT_EQ(st.findings, 1u);
    EXPECT_EQ(st.byRule[static_cast<size_t>(BusRule::EarlyAccess)], 1u);
    const auto findings = tap.Findings(BusFindingQuery{});
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].value, 0x12);
    EXPECT_EQ(findings[0].eventSeq, 2u) << "the early access itself is the event";
    tap.Control(0, 0, true);
    EXPECT_FALSE(tap.Active());
    EXPECT_EQ(tap.Status().capacity, 0u);
}

TEST(BusManager_Test, RegistersUnregistersAndSuffixesATakenId)
{
    EmulatorContext context(LoggerLevel::LogError);
    BusManager manager;
    context.pBusManager = &manager;
    {
        BusTap a("zc.spi", BusProtocol::Spi, "a");
        BusTap b("zc.spi", BusProtocol::Spi, "b");
        a.Attach(&context);
        b.Attach(&context);
        EXPECT_EQ(manager.Ids(), (std::vector<std::string>{"zc.spi", "zc.spi.2"}));
        bool seen = false;
        EXPECT_TRUE(manager.With("zc.spi.2", [&](BusTap& tap) { seen = &tap == &b; }));
        EXPECT_TRUE(seen);
        EXPECT_FALSE(manager.With("none", [](BusTap&) {}));
        manager.NextEpoch();
    }
    EXPECT_EQ(manager.Count(), 0u) << "a tap unregisters when it goes";
    context.pBusManager = nullptr;
}

/// endregion

/// region <SD card: protocol events and checks>

TEST(SdCardBus_Test, CommandsAndBlocksReachTheTrace)
{
    CardOnTap t;
    ASSERT_GT(Init(t.card), 0);
    ASSERT_EQ(Cmd(t.card, 17, 3), 0x00);
    BusEventQuery q;
    std::string error;
    ASSERT_TRUE(BusEventQueryFromStrings("sd", "", "", "", "", "0", q, error));
    const auto events = t.tap.Events(q);
    ASSERT_GE(events.size(), 4u);
    EXPECT_EQ(events[0].kind, BusEventKind::SdCommand);
    EXPECT_EQ(events[0].a, 0);
    EXPECT_EQ(events[0].b, 0x01) << "R1 idle";
    EXPECT_TRUE(events[0].flags & BusEventFlag::CrcValid);
    const auto acmd = std::find_if(events.begin(), events.end(), [](const BusTraceEvent& e) { return e.a == 41; });
    ASSERT_NE(acmd, events.end());
    EXPECT_TRUE(acmd->flags & BusEventFlag::App);
    EXPECT_EQ(events.back().kind, BusEventKind::SdCommand);
    EXPECT_EQ(events.back().a, 17);
    EXPECT_EQ(events[events.size() - 2].kind, BusEventKind::SdReadBlock) << "the block is queued while the command is decoded";
    EXPECT_EQ(events[events.size() - 2].block, 3u);
    EXPECT_EQ(CountRule(t.tap, BusRule::CommandDropsAnswer), 0u);
}

TEST(SdCardBus_Test, ACommandBeforeTheBlockWasReadOutIsFound)
{
    CardOnTap t;
    ASSERT_GT(Init(t.card), 0);
    ASSERT_EQ(Cmd(t.card, 17, 1), 0x00);
    ASSERT_EQ(Cmd(t.card, 13, 0), 0x00) << "the ideal card forgives it";
    EXPECT_EQ(CountRule(t.tap, BusRule::CommandDropsAnswer), 1u);

    // Read out completely: quiet
    ASSERT_EQ(Cmd(t.card, 17, 1), 0x00);
    ASSERT_GE(ToToken(t.card, 64), 0);
    for (int i = 0; i < 512 + 2; i++)
        X(t.card, 0xFF);
    Cmd(t.card, 13, 0);
    EXPECT_EQ(CountRule(t.tap, BusRule::CommandDropsAnswer), 1u);
}

TEST(SdCardBus_Test, ACommandWhileTheCardIsBusyIsFound)
{
    CardOnTap t;
    ASSERT_GT(Init(t.card), 0);
    ASSERT_EQ(WriteBlock(t.card, 2), 0x05);
    Cmd(t.card, 13, 0);  // no busy poll
    EXPECT_EQ(CountRule(t.tap, BusRule::CommandWhileBusy), 1u);

    ASSERT_EQ(WriteBlock(t.card, 3), 0x05);
    ASSERT_GE(BusyBytes(t.card, 1000), 0);
    Cmd(t.card, 13, 0);
    EXPECT_EQ(CountRule(t.tap, BusRule::CommandWhileBusy), 1u) << "after the busy poll: quiet";
}

TEST(SdCardBus_Test, DeselectInsideACommandAndACrcErrorAreFound)
{
    CardOnTap t;
    ASSERT_GT(Init(t.card), 0);
    X(t.card, 0x51);
    X(t.card, 0x00);
    t.card.select(false);
    t.card.select(true);
    EXPECT_EQ(CountRule(t.tap, BusRule::DeselectMidCommand), 1u);
    EXPECT_NE(Cmd(t.card, 8, 0x1AA, 0xFF) & 0x08, 0) << "CMD8 is always CRC-checked";
    EXPECT_EQ(CountRule(t.tap, BusRule::CrcError), 1u);
}

/// endregion

/// region <SD card: timing profiles>

TEST(SdCardTiming_Test, SlowMeetsTheSpecificationsWorstCaseWithoutAClock)
{
    SdCardSpi card;
    card.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
    card.SetTiming(SdCardSpi::Timing::Slow);
    card.select(true);
    const int polls = Init(card, 5000);
    EXPECT_GE(polls, 1000) << "1 s of initialization, one poll per ms without a clock";
    EXPECT_LT(polls, 1100);
    ASSERT_EQ(Cmd(card, 17, 1), 0x00);
    EXPECT_GE(ToToken(card, 200000), 100000) << "100 ms to the data token";
    for (int i = 0; i < 514; i++)
        X(card, 0xFF);
    ASSERT_EQ(WriteBlock(card, 1), 0x05);
    EXPECT_GE(BusyBytes(card, 400000), 250000) << "250 ms of write busy";
}

TEST(SdCardTiming_Test, RealisticIsDeterministicBySeed)
{
    auto latency = [](uint64_t seed) {
        SdCardSpi card;
        card.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
        card.SetTiming(SdCardSpi::Timing::Realistic, seed);
        card.select(true);
        const int polls = Init(card, 1000);
        Cmd(card, 17, 1);
        const int token = ToToken(card, 5000);
        return std::make_pair(polls, token);
    };
    const auto a = latency(7);
    EXPECT_EQ(a, latency(7));
    EXPECT_GE(a.first, 50);
    EXPECT_LE(a.first, 501);
    EXPECT_GE(a.second, 100);
    EXPECT_LE(a.second, 2000);
    EXPECT_NE(a, latency(8)) << "another seed, other delays";
}

TEST(SdCardTiming_Test, ClockedDelaysEndOnTimeAcrossADeselect)
{
    uint64_t now = 0;
    SdCardSpi card;
    card.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
    card.SetClock([&now]() { return now; });
    card.select(true);
    ASSERT_GT(Init(card), 0) << "ideal while the profile is not set";
    card.SetTiming(SdCardSpi::Timing::Slow);
    ASSERT_EQ(Cmd(card, 17, 1), 0x00);
    EXPECT_EQ(ToToken(card, 1000), -1) << "the clock stands still: no token";
    card.select(false);
    now += 100000;  // the driver waits 100 ms deselected
    card.select(true);
    EXPECT_GE(ToToken(card, 4), 0) << "the time passed while deselected counts";
}

TEST(SdCardTiming_Test, TheProfileAndAPendingDelaySurviveTheState)
{
    SdCardSpi card;
    card.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
    card.select(true);
    ASSERT_GT(Init(card), 0);
    card.SetTiming(SdCardSpi::Timing::Realistic, 42);
    ASSERT_EQ(Cmd(card, 17, 1), 0x00);
    X(card, 0xFF);
    ASSERT_TRUE(card.DelayPending());
    std::vector<uint8_t> state(SdCardSpi::STATE_SIZE);
    card.saveState(state.data());
    EXPECT_EQ(state[0], 2) << "layout version 2";

    SdCardSpi copy;
    copy.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
    copy.loadState(state.data());
    EXPECT_EQ(copy.timing(), SdCardSpi::Timing::Realistic);
    EXPECT_EQ(copy.timingSeed(), 42u);
    EXPECT_TRUE(copy.DelayPending());
    EXPECT_EQ(ToToken(copy, 5000), ToToken(card, 5000)) << "both continue the same delay";

    state[0] = 1;  // a version 1 state: no timing
    copy.loadState(state.data());
    EXPECT_EQ(copy.timing(), SdCardSpi::Timing::Ideal);
    EXPECT_FALSE(copy.DelayPending());
}

TEST(SdCardTiming_Test, ARequestFromAnotherThreadAppliesAtTheNextAccess)
{
    SdCardSpi card;
    card.RequestTiming(SdCardSpi::Timing::Slow, 3);
    EXPECT_TRUE(card.TimingRequestPending());
    EXPECT_EQ(card.timing(), SdCardSpi::Timing::Slow) << "reported as requested";
    card.exchange(0xFF);
    EXPECT_FALSE(card.TimingRequestPending());
    EXPECT_EQ(card.timing(), SdCardSpi::Timing::Slow);
    EXPECT_EQ(card.timingSeed(), 3u);
}

/// endregion

/// region <Masters>

TEST(NeoGSSpiBus_Test, ARestartMidByteIsAnEarlyAccessAndATruncatedByte)
{
    BusTap tap("neogs.sd", BusProtocol::Spi, "test");
    tap.Control(1, 1, false);
    SpiTapDevice device(tap, 0);
    NeoGSSpi spi;
    spi.attach(NeoGSSpi::SD, &device);
    spi.start(NeoGSSpi::SD, 0x40, 0, 1);
    spi.start(NeoGSSpi::SD, 0x41, 4, 1);  // 4 clocks into a 16-clock byte
    spi.sync(100);
    const auto events = tap.Events(All());
    EXPECT_EQ(CountKind(events, BusEventKind::SpiTruncated), 1u);
    ASSERT_EQ(CountKind(events, BusEventKind::SpiByte), 1u) << "only the second byte reached the device";
    EXPECT_EQ(events.back().a, 0x41);
    EXPECT_EQ(CountRule(tap, BusRule::EarlyAccess), 1u);
}

TEST(DivMmcBus_Test, SelectsAndBytesAreTraced)
{
    DivMmcPaging divmmc(nullptr);
    BusTap& tap = divmmc.SpiTap();
    tap.Control(1, 1, false);
    divmmc.Card(0).insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC);
    divmmc.portDeviceOutMethod(0xE7, 0xFE);
    for (uint8_t b : {0x40, 0x00, 0x00, 0x00, 0x00, 0x95})
        divmmc.portDeviceOutMethod(0xEB, b);
    for (int i = 0; i < 4; i++)
        divmmc.portDeviceInMethod(0xEB);
    divmmc.portDeviceOutMethod(0xE7, 0xFF);
    const auto events = tap.Events(All());
    EXPECT_EQ(CountKind(events, BusEventKind::SpiSelect), 1u);
    EXPECT_EQ(CountKind(events, BusEventKind::SpiDeselect), 1u);
    EXPECT_EQ(CountKind(events, BusEventKind::SpiByte), 10u);
    ASSERT_EQ(CountKind(events, BusEventKind::SdCommand), 1u);
    const auto cmd = std::find_if(events.begin(), events.end(), [](const BusTraceEvent& e) { return e.kind == BusEventKind::SdCommand; });
    EXPECT_EQ(cmd->b, 0x01);
}

class TsConfBus_Test : public TsConfFixture
{
protected:
    BusTap& Tap() { return _decoder->SpiTap(); }
    void StartSpiDmaRead()
    {
        Reg(TsConfReg::DmaDAl, 0x00);
        Reg(TsConfReg::DmaDAh, 0x00);
        Reg(TsConfReg::DmaDAx, 0x14);
        Reg(TsConfReg::DmaLen, 0xFF);
        Reg(TsConfReg::DmaNum, 0x00);
        _decoder->GetEngine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        Reg(TsConfReg::DmaCtrl, 0x02);
    }
    void FinishDma()
    {
        for (uint32_t line = 1; line < 320 && _decoder->GetDma().Busy(); line++)
            _decoder->GetEngine().CatchUp(line * TsConfEngine::kLineTacts);
    }
};

TEST_F(TsConfBus_Test, TheBootSequenceIsInTheTrace)
{
    Tap().Control(1, 1, false);
    ASSERT_TRUE(_decoder->InsertSdCard(PatternDisk(64), SdCardSpi::WriteMode::Session));
    Out(0x0077, 0x00);
    ASSERT_TRUE(SdInit(_decoder, 0x0057));
    ASSERT_EQ(SdCommand(_decoder, 0x0057, 17, 5 * 512), 0x00);
    const auto events = Tap().Events(All());
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().kind, BusEventKind::SpiSelect);
    EXPECT_EQ(events.front().slot, 0);
    std::vector<std::string> commands;
    for (const BusTraceEvent& e : events)
        if (e.kind == BusEventKind::SdCommand)
            commands.push_back(SdCommandName(e.a, (e.flags & BusEventFlag::App) != 0));
    ASSERT_GE(commands.size(), 4u);
    EXPECT_EQ(commands.front(), "CMD0");
    EXPECT_EQ(commands[1], "CMD55");
    EXPECT_EQ(commands[2], "ACMD41");
    EXPECT_EQ(commands.back(), "CMD17");
    EXPECT_EQ(CountKind(events, BusEventKind::SdReadBlock), 1u);
    const auto byte = std::find_if(events.begin(), events.end(), [](const BusTraceEvent& e) { return e.kind == BusEventKind::SpiByte; });
    ASSERT_NE(byte, events.end());
    EXPECT_EQ(byte->a, 0x40) << "CMD0's first byte";
    EXPECT_EQ(byte->source, BusSource::Cpu);
    EXPECT_EQ(Tap().Status().findings, 0u) << "a correct driver: no finding";
}

TEST_F(TsConfBus_Test, TheCpuOnTheDataPortDuringAnSpiDmaIsACollision)
{
    ASSERT_TRUE(_decoder->InsertSdCard(PatternDisk(64), SdCardSpi::WriteMode::Session));
    Out(0x0077, 0x00);
    ASSERT_TRUE(SdInit(_decoder, 0x0057));
    ASSERT_EQ(SdCommand(_decoder, 0x0057, 17, 3 * 512), 0x00);
    bool token = false;
    for (int i = 0; i < 64 && !token; i++)
        token = In(0x0057) == 0xFE;
    ASSERT_TRUE(token);
    Tap().Control(1, 1, false);
    StartSpiDmaRead();
    In(0x0057);  // the driver does not wait for the DMA
    EXPECT_EQ(CountRule(Tap(), BusRule::DmaCollision), 1u);
    FinishDma();
    const auto events = Tap().Events(All());
    const size_t dmaBytes = static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const BusTraceEvent& e) {
        return e.kind == BusEventKind::SpiByte && e.source == BusSource::Dma;
    }));
    EXPECT_GT(dmaBytes, 400u) << "the DMA's bytes are the DMA's";
    In(0x0057);  // after the DMA: quiet
    EXPECT_EQ(CountRule(Tap(), BusRule::DmaCollision), 1u);
}

class NextBus_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(NextBus_Test, AnAccessSoonerThanAByteIsAnEarlyAccessOnTheBus)
{
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    const StateNode on = DeviceState::BusControl(_context, "next.spi", 1, 1, true);
    ASSERT_TRUE(on.find("available")->b);
    _ports->DecodePortOut(0xE7, 0xFE, 0);
    _z80->t += 20;
    _ports->DecodePortOut(0xEB, 0x40, 0);
    _ports->DecodePortOut(0xEB, 0x00, 0);  // no byte time in between
    EXPECT_EQ(_ports->SpiTooFastCount(), 1u) << "the existing counter still counts";
    BusFindingQuery q;
    const StateNode findings = DeviceState::BusFindings(_context, "next.spi", q);
    const StateNode* list = findings.find("findings");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->items.size(), 1u);
    EXPECT_EQ(list->items[0].find("rule")->s, "early_access");
    EXPECT_EQ(list->items[0].find("severity")->s, "error");
    EXPECT_EQ(list->items[0].find("slot")->i, 0);
}

/// endregion

/// region <ATA (mutation-tests.md F-ATA): the reference sequence and its mutants A1-A3>

namespace
{
struct AtaBench
{
    BusTap tap{"ide0", BusProtocol::Ata, "test"};
    AtaChannel channel;
    std::unique_ptr<MemoryDisk> medium = std::make_unique<MemoryDisk>(64);
    AtaBench()
    {
        auto disk = std::make_unique<AtaDisk>();
        disk->AttachMedium(*medium, {});
        channel.SetUnit(0, std::move(disk));
        channel.SetTap(&tap);
        tap.Control(1, 1, false);
    }
    uint8_t Status() { return channel.ReadRegister(ata::StatusCommand); }
    /// A driver's wait: status until BSY falls and `bits` are set
    bool WaitFor(uint8_t bits)
    {
        for (int i = 0; i < 100; i++)
        {
            const uint8_t s = Status();
            if (!(s & ata::Status::BSY) && (s & bits) == bits)
                return true;
        }
        return false;
    }
    void Command(uint8_t command, uint32_t lba = 0)
    {
        channel.WriteRegister(ata::DeviceHead, static_cast<uint8_t>(ata::DeviceBits::LBA | ((lba >> 24) & 0x0F)));
        channel.WriteRegister(ata::SectorCount, 1);
        channel.WriteRegister(ata::SectorNumber, static_cast<uint8_t>(lba));
        channel.WriteRegister(ata::CylinderLow, static_cast<uint8_t>(lba >> 8));
        channel.WriteRegister(ata::CylinderHigh, static_cast<uint8_t>(lba >> 16));
        channel.WriteRegister(ata::StatusCommand, command);
    }
    void ReadWords(int words)
    {
        for (int i = 0; i < words; i++)
            channel.ReadData();
    }
};
}  // namespace

TEST(AtaBus_Test, TheReferenceSequenceIsCleanAndTraced)
{
    AtaBench b;
    ASSERT_TRUE(b.WaitFor(ata::Status::DRDY));
    b.Command(ata::Command::Identify);
    ASSERT_TRUE(b.WaitFor(ata::Status::DRQ));
    b.ReadWords(256);
    b.Command(ata::Command::ReadSectors, 3);
    ASSERT_TRUE(b.WaitFor(ata::Status::DRQ));
    b.ReadWords(256);
    b.Command(ata::Command::WriteSectors, 4);
    ASSERT_TRUE(b.WaitFor(ata::Status::DRQ));
    for (int i = 0; i < 256; i++)
        b.channel.WriteData(0x1234);
    ASSERT_TRUE(b.WaitFor(ata::Status::DRDY));
    EXPECT_EQ(b.tap.Status().findings, 0u);
    BusEventQuery q;
    std::string error;
    ASSERT_TRUE(BusEventQueryFromStrings("ata_command", "", "", "", "", "0", q, error));
    const auto commands = b.tap.Events(q);
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(AtaCommandName(commands[0].a), "IDENTIFY DEVICE (#EC)");
    EXPECT_EQ(AtaCommandName(commands[1].a), "READ SECTORS (#20)");
    ASSERT_TRUE(BusEventQueryFromStrings("data", "", "", "", "", "0", q, error));
    EXPECT_EQ(b.tap.Events(q).size(), 768u) << "every data word";
}

/// A1: data read without waiting for DRQ (the driver ignores the status)
TEST(AtaBus_Test, A1_DataWithoutDrq)
{
    AtaBench b;
    ASSERT_TRUE(b.WaitFor(ata::Status::DRDY));
    b.ReadWords(1);
    EXPECT_EQ(CountRule(b.tap, BusRule::AtaDataWithoutDrq), 1u);
}

/// A2: a new command while the previous sector is half read
TEST(AtaBus_Test, A2_ACommandDuringATransferDropsTheData)
{
    AtaBench b;
    ASSERT_TRUE(b.WaitFor(ata::Status::DRDY));
    b.Command(ata::Command::ReadSectors, 1);
    ASSERT_TRUE(b.WaitFor(ata::Status::DRQ));
    b.ReadWords(100);
    b.Command(ata::Command::ReadSectors, 2);
    EXPECT_EQ(CountRule(b.tap, BusRule::AtaCommandDropsData), 1u);
    EXPECT_EQ(CountRule(b.tap, BusRule::AtaWriteWhileBusy), 0u);
}

/// A3: task-file writes during a software reset (SRST held: BSY), without waiting for BSY to fall
TEST(AtaBus_Test, A3_RegisterWritesWhileBusy)
{
    AtaBench b;
    b.channel.WriteRegister(ata::Control, ata::DeviceControl::SRST);
    b.channel.WriteRegister(ata::SectorCount, 1);
    EXPECT_EQ(CountRule(b.tap, BusRule::AtaWriteWhileBusy), 1u) << "BSY during the reset: the write is ignored by a drive";
    b.channel.WriteRegister(ata::Control, 0);  // the reset ends
    ASSERT_TRUE(b.WaitFor(ata::Status::DRDY));
    b.channel.WriteRegister(ata::SectorCount, 1);
    EXPECT_EQ(CountRule(b.tap, BusRule::AtaWriteWhileBusy), 1u) << "after the wait: quiet";
}

/// endregion

/// region <Reports, text, export, gate>

TEST(BusReports_Test, TheTsConfMachineListsItsBusAndAnswersEveryPlaneTheSame)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    const StateNode list = DeviceState::BusList(context);
    const StateNode* buses = list.find("buses");
    ASSERT_NE(buses, nullptr);
    const StateNode* found = nullptr;
    for (const StateNode& bus : buses->items)
        if (bus.find("id")->s == "zc.spi")
            found = &bus;
    ASSERT_NE(found, nullptr) << "TS-Conf has the Z-Controller bus (and its IDE channel)";
    const StateNode& zc = *found;
    EXPECT_EQ(zc.find("protocol")->s, "spi");
    const StateNode& slot0 = zc.find("devices")->items[0];
    EXPECT_EQ(slot0.find("name")->s, "sd");
    ASSERT_NE(slot0.find("sd"), nullptr);
    EXPECT_EQ(slot0.find("sd")->find("timing")->s, "ideal");
    EXPECT_FALSE(zc.find("trace")->find("enabled")->b) << "off by default";

    const StateNode all = DeviceState::BusControl(context, "all", 1, 1, false);
    for (const StateNode& bus : all.find("buses")->items)
        EXPECT_TRUE(bus.find("trace")->find("enabled")->b) << bus.find("id")->s;

    const StateNode none = DeviceState::BusEvents(context, "next.spi", BusEventQuery{});
    EXPECT_FALSE(none.find("available")->b);
    EXPECT_NE(none.find("description")->s.find("zc.spi"), std::string::npos) << "the error lists the machine's buses";

    EXPECT_FALSE(DeviceState::BusSdTiming(context, "zc.spi", -1, "fast", 0).find("available")->b);
    const StateNode timing = DeviceState::BusSdTiming(context, "zc.spi", -1, "slow", 5);
    ASSERT_TRUE(timing.find("available")->b);
    const StateNode* sd = timing.find("devices")->items[0].find("sd");
    EXPECT_EQ(sd->find("timing")->s, "slow");
    EXPECT_EQ(sd->find("seed")->i, 5);

    const std::string text = DeviceState::BusText(DeviceState::BusList(context));
    EXPECT_NE(text.find("zc.spi (spi): Z-Controller SPI"), std::string::npos) << text;
    EXPECT_NE(text.find("trace on"), std::string::npos) << text;
    EXPECT_NE(text.find("slot 0: sd sd present="), std::string::npos) << text;
    EXPECT_NE(text.find("timing=slow"), std::string::npos) << text;

    DeviceState::BusSdTiming(context, "zc.spi", -1, "ideal", 1);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(BusReports_Test, EventTextCarriesEveryField)
{
    StateNode report = StateNode::Object();
    report["available"] = true;
    report["id"] = "zc.spi";
    report["protocol"] = "spi";
    report["label"] = "l";
    report["trace"]["enabled"] = true;
    report["checks"]["enabled"] = true;
    StateNode e = StateNode::Object();
    e["seq"] = 7;
    e["frame"] = 12;
    e["t"] = 3456;
    e["pc"] = "0x8000";
    e["source"] = "cpu";
    e["slot"] = 0;
    e["kind"] = "command";
    e["command"] = "CMD17";
    e["argument"] = "0x00000A00";
    e["r1"] = "0x00";
    e["crc_valid"] = true;
    report["events"].push(e);
    const std::string text = DeviceState::BusText(report);
    EXPECT_NE(text.find("#7 f12 t3456 pc=0x8000 cpu slot=0 command CMD17 arg=0x00000A00 r1=0x00"), std::string::npos) << text;
}

TEST(BusExport_Test, VcdHasTheWiresAndJsonLinesEveryEvent)
{
    BusTapStatus status;
    status.id = "zc.spi";
    status.label = "test";
    status.byteTimeNs = 800;
    status.devices = {BusDevice{0, "sd"}};
    std::vector<BusTraceEvent> events = {At(0, 0, BusEventKind::SpiSelect), At(0, 10, BusEventKind::SpiByte), At(0, 20, BusEventKind::SpiDeselect)};
    events[1].a = 0x80;  // MOSI: only the first bit high
    events[1].b = 0x01;  // MISO: only the last bit high
    const std::string vcd = BusExportVcd(status, events, BusExportTiming{});
    EXPECT_NE(vcd.find("$timescale 1ns $end"), std::string::npos);
    EXPECT_NE(vcd.find("$var wire 1 ! sck $end"), std::string::npos);
    EXPECT_NE(vcd.find("cs_n_sd"), std::string::npos);
    EXPECT_NE(vcd.find("\n0$\n"), std::string::npos) << "cs_n_sd falls at the select: " << vcd.substr(0, 400);
    EXPECT_EQ(std::count(vcd.begin(), vcd.end(), '#') >= 17, true) << "8 bits x 2 edges + the end";
    const std::string jsonl = BusExportJsonLines(status, events);
    EXPECT_EQ(std::count(jsonl.begin(), jsonl.end(), '\n'), 4) << "the header and three events";
    EXPECT_NE(jsonl.find("\"kind\":\"byte\",\"a\":128,\"b\":1"), std::string::npos) << jsonl;
}

TEST(BusGate_Test, OnlyTheSdTimingChangesTheMachine)
{
    EXPECT_TRUE(automation_gate::CliCommandChangesMachine("bus", {"zc.spi", "sd-timing", "slow"}));
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bus", {"zc.spi", "sd-timing"}));
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bus", {"zc.spi", "on"}));
    EXPECT_FALSE(automation_gate::CliCommandChangesMachine("bus", {"zc.spi", "events"}));
    EXPECT_FALSE(automation_gate::ChangesMachine("POST", "/bus/zc.spi/trace-journal"));
    EXPECT_TRUE(automation_gate::ChangesMachine("POST", "/bus/zc.spi/sd-timing"));
    EXPECT_FALSE(automation_gate::ChangesMachine("GET", "/bus/zc.spi/events"));
}

/// endregion
