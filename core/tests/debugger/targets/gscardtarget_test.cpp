// The General Sound card CPU as a debug target (gs-debugger design §3-§4): its own breakpoints, the card hooks
// swapped in only while a breakpoint asks for them, execute stops before the instruction, watchpoints and card
// events reported at the next instruction boundary with the PC of the instruction that did the access.
//
// The card runs the real gs105a firmware by itself (no machine): a stop is recorded by the target (LastStop), there
// is no emulator to pause, so the card runs on past it. After POST the firmware waits for the host to take its reply
// byte, then polls the mailbox (boot()).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "unrealasm/symbols/symbol.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/targets/cpuview.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/slots/cards/multisound/multisoundstagedmachine.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "unrealasm/symbols/symbol.h"

namespace
{
// gs105a: COMINT_, the idle poll loop after POST (IN A,(4) / RRCA / JR C ...), interrupts off
constexpr uint16_t kIdleLoop = 0x026E;

struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;
    std::unique_ptr<GSCardTarget> target;

    Card()
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM("rom/gs105a.rom");
        target = std::make_unique<GSCardTarget>(&ctx, [this]() -> GeneralSoundCard* { return chip.get(); });
    }
    ~Card() { target.reset(); }  // the hooks leave the card before it goes

    BreakpointManager& bp() { return *target->GetBreakpointManager(); }

    uint16_t add(const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = bp().AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }

    /// POST, then the host takes the reply byte: the firmware is in its poll loop
    void boot()
    {
        for (int i = 0; i < 200 && chip->getActivityCounters().volumeLatchWrites < 4; i++)
            frame();
        frame();
        if (chip->readStatus() & 0x80)
            (void)chip->portDeviceInMethod(GeneralSoundCard::PORT_DATA);
        frame();
    }

    /// Runs up to `frames` frames, stopping as soon as the target recorded a stop
    bool runUntilStop(int frames)
    {
        const uint64_t before = target->StopCount();
        for (int f = 0; f < frames && target->StopCount() == before; f++)
            frame();
        return target->StopCount() != before;
    }
};

BreakpointSpec Exec(uint16_t address)
{
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = address;
    return spec;
}

BreakpointSpec Event(GSCardEvent kind)
{
    BreakpointSpec spec;
    spec.type = BRK_EVENT;
    spec.access = 0;
    spec.address = static_cast<uint16_t>(kind);
    return spec;
}
}  // namespace

TEST(GSCardTarget_Test, TheHooksAreOnOnlyWhileABreakpointAsks)
{
    Card card;
    EXPECT_FALSE(card.target->HooksAttached());
    card.boot();
    card.frame();  // the fast loop sleeps through the poll loop
    EXPECT_GT(card.chip->stepsSlept(), 0u);

    const uint16_t id = card.add(Exec(0x3FFF));  // never executed
    EXPECT_TRUE(card.target->HooksAttached());
    const uint64_t slept = card.chip->stepsSlept();
    for (int f = 0; f < 3; f++)
        card.frame();
    EXPECT_EQ(card.chip->stepsSlept(), slept) << "the debug loop never sleeps: every instruction is checked";

    card.bp().DeactivateBreakpoint(id);
    EXPECT_FALSE(card.target->HooksAttached()) << "an inactive breakpoint does not hold the hooks";
    card.bp().ActivateBreakpoint(id);
    EXPECT_TRUE(card.target->HooksAttached());
    card.bp().RemoveBreakpointByID(id);
    EXPECT_FALSE(card.target->HooksAttached());
    card.frame();
    EXPECT_GT(card.chip->stepsSlept(), slept) << "the fast loop is back";
}

TEST(GSCardTarget_Test, AnExecuteBreakpointStopsBeforeItsInstruction)
{
    Card card;
    card.boot();
    const uint16_t id = card.add(Exec(kIdleLoop));
    ASSERT_TRUE(card.runUntilStop(2));
    const GSCardTarget::Stop stop = card.target->LastStop();
    EXPECT_EQ(stop.breakpointId, id);
    EXPECT_EQ(stop.kind, BreakpointHitKind::Execute);
    EXPECT_EQ(stop.address, kIdleLoop);
    EXPECT_EQ(stop.pc, kIdleLoop);
    EXPECT_EQ(stop.boundaryPc, kIdleLoop) << "the card stood before the instruction";
}

TEST(GSCardTarget_Test, APageTiedBreakpointMatchesThePageInAnyWindow)
{
    Card card;
    // MPAG 0 after reset: ROM page 0 at #0000 and again at #8000
    const MemoryPageDescriptor low = card.target->MapAddress(kIdleLoop);
    const MemoryPageDescriptor high = card.target->MapAddress(0x8000 | kIdleLoop);
    EXPECT_EQ(low.mode, BANK_ROM);
    EXPECT_EQ(low.page, 0);
    EXPECT_EQ(low.addressInPage, kIdleLoop);
    EXPECT_EQ(high.mode, BANK_ROM);
    EXPECT_EQ(high.page, 0);
    EXPECT_EQ(card.target->MapAddress(0x4000).mode, BANK_RAM);
    EXPECT_EQ(card.target->PageCount(BANK_ROM), 2u);
    EXPECT_EQ(card.target->PageCount(BANK_RAM), 32u);  // 512 KB

    BreakpointSpec spec = Exec(kIdleLoop);
    spec.hasPage = true;
    spec.pageType = BANK_ROM;
    spec.page = 0;
    card.boot();
    card.add(spec);
    ASSERT_TRUE(card.runUntilStop(2));
    EXPECT_EQ(card.target->LastStop().address, kIdleLoop);

    std::string error;
    spec.page = 2;
    EXPECT_EQ(card.bp().AddBreakpoint(spec, error), BRK_INVALID) << "the card has two ROM pages";
}

TEST(GSCardTarget_Test, AWatchpointIsReportedAtTheNextBoundaryWithTheAccessPc)
{
    Card card;
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_WRITE;
    spec.address = 0x4000;
    spec.hasEnd = true;
    spec.addressEnd = 0x7FFF;
    card.add(spec);
    ASSERT_TRUE(card.runUntilStop(120));
    const GSCardTarget::Stop stop = card.target->LastStop();
    EXPECT_EQ(stop.kind, BreakpointHitKind::MemoryWrite);
    EXPECT_GE(stop.address, 0x4000);
    EXPECT_LE(stop.address, 0x7FFF);
    uint8_t byte = 0;
    ASSERT_TRUE(card.chip->peekCardMemory(stop.address, byte));
    EXPECT_EQ(byte, stop.value) << "the write is done when it is reported";
    EXPECT_NE(stop.pc, stop.boundaryPc) << "reported after the writing instruction, before the next one";
}

TEST(GSCardTarget_Test, APortBreakpointSeesTheIdlePoll)
{
    Card card;
    BreakpointSpec spec;
    spec.type = BRK_IO;
    spec.access = BRK_IO_IN;
    spec.address = 0x04;  // FLAGS
    spec.portMask = 0x00FF;
    card.boot();
    card.add(spec);
    ASSERT_TRUE(card.runUntilStop(2));
    const GSCardTarget::Stop stop = card.target->LastStop();
    EXPECT_EQ(stop.kind, BreakpointHitKind::PortIn);
    EXPECT_EQ(stop.address & 0xFF, 0x04);
}

/// A breakpoint of the card CPU that only counts (PLAN #122 V4): every hit counted as a stopping one counts them, the
/// newest recorded with the card's moment (the ZX frame, its time since the frame's start, its own clock) and the
/// instruction doing the access; the newest record is the last stop
TEST(GSCardTarget_Test, ACountingBreakpointRecordsTheCardsMoments)
{
    Card card;
    BreakpointSpec spec;
    spec.type = BRK_IO;
    spec.access = BRK_IO_IN;
    spec.address = 0x04;  // FLAGS
    spec.portMask = 0x00FF;
    BreakpointSpec counting = spec;
    counting.stop = false;
    counting.hitLog = 16;
    counting.hitLogKeep = BreakpointHitLogKeep::Last;
    card.boot();
    const uint16_t stopId = card.add(spec);
    const uint16_t countId = card.add(counting);
    card.ctx.emulatorState.frame_counter = 7;
    card.frame();

    const BreakpointManager::HitLogInfo info = card.bp().GetHitLog(countId, 0, 100);
    ASSERT_TRUE(info.found && info.countOnly && info.hasLog);
    EXPECT_GT(info.hitCount, 16u) << "the idle poll reads FLAGS all the frame";
    EXPECT_EQ(info.hitCount, card.bp().GetBreakpointById(stopId)->hitCount) << "as many hits as the stopping one";
    ASSERT_EQ(info.view.records.size(), 16u);
    EXPECT_EQ(info.view.oldest, info.hitCount - 16u);
    const GSCardTarget::Stop stop = card.target->LastStop();
    const BreakpointHitRecord& newest = info.view.records.back();
    EXPECT_EQ(newest.hit, info.hitCount);
    EXPECT_EQ(newest.pc, stop.pc) << "the instruction doing the read";
    EXPECT_EQ(newest.address, stop.address);
    EXPECT_EQ(newest.kind, BRK_TAP_PORT_IN);
    for (size_t i = 0; i < info.view.records.size(); ++i)
    {
        const BreakpointHitRecord& r = info.view.records[i];
        EXPECT_TRUE(r.frame == 7 || r.frame == 6) << "the ZX frame the card runs in";
        EXPECT_GE(r.cpuTime, 0) << "the card's own clock";
        if (i > 0)
        {
            EXPECT_GT(r.cpuTime, info.view.records[i - 1].cpuTime);
            EXPECT_EQ(r.hit, info.view.records[i - 1].hit + 1);
        }
    }
}

TEST(GSCardTarget_Test, CardEventsStopTheCard)
{
    Card card;
    card.boot();

    // The host's command, read by the firmware; the value filter takes only this one
    BreakpointSpec spec = Event(GSCardEvent::CardCommandRead);
    spec.nrValue = 0x23;  // "get the firmware version" style command: any byte the firmware reads
    spec.nrMask = 0xFF;
    const uint16_t command = card.add(spec);
    card.chip->sendCommand(0x22);
    card.frame();
    EXPECT_EQ(card.target->StopCount(), 0u) << "another command is filtered out";
    card.chip->sendCommand(0x23);
    ASSERT_TRUE(card.runUntilStop(5));
    EXPECT_EQ(card.target->LastStop().breakpointId, command);
    EXPECT_EQ(card.target->LastStop().kind, BreakpointHitKind::Event);
    EXPECT_EQ(card.target->LastStop().address, static_cast<uint16_t>(GSCardEvent::CardCommandRead));
    EXPECT_EQ(card.target->LastStop().value, 0x23);

    // A reset (host #33 bit 7)
    const uint16_t reset = card.add(Event(GSCardEvent::Reset));
    card.chip->resetCard();
    ASSERT_TRUE(card.runUntilStop(1)) << "reported at the first boundary after it";
    EXPECT_EQ(card.target->LastStop().breakpointId, reset);
    EXPECT_EQ(card.target->LastStop().boundaryPc, 0x0000);

    // POST after the reset writes MPAG
    const uint16_t page = card.add(Event(GSCardEvent::PageSwitch));
    ASSERT_TRUE(card.runUntilStop(200));
    EXPECT_EQ(card.target->LastStop().breakpointId, page);
}

TEST(GSCardTarget_Test, TheCardTakesItsOwnKindsOnly)
{
    Card card;
    std::string error;
    BreakpointSpec magic;
    magic.type = BRK_MAGIC;
    magic.address = BRK_MAGIC_ED_FF;
    EXPECT_EQ(card.bp().AddBreakpoint(magic, error), BRK_INVALID);

    EXPECT_EQ(card.bp().AddBreakpoint(Event(GSCardEvent::HostCommandWrite), error), BRK_INVALID)
        << "the host side is the main CPU's";
    EXPECT_FALSE(card.target->HooksAttached());
}

TEST(GSCardTarget_Test, AConditionReadsTheCardsRegistersAndMemory)
{
    Card card;
    card.boot();
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card.chip->cardRegisters(regs));
    char text[96];
    // The poll loop's IN A,(4) at #026E (DB 04) in ROM page 0, the card's own stack
    std::snprintf(text, sizeof(text), "SP == #%04X && M(#%04X) == #DB && W(#%04X) == #04DB && ROMPG == 0", regs.sp,
                  kIdleLoop, kIdleLoop);
    BreakpointSpec spec = Exec(kIdleLoop);
    spec.condition = text;
    const uint16_t id = card.add(spec);
    ASSERT_TRUE(card.runUntilStop(2));
    EXPECT_EQ(card.target->LastStop().breakpointId, id);
    const BreakpointSnapshot held = card.bp().GetBreakpointById(id);
    ASSERT_TRUE(held);
    EXPECT_EQ(held->conditionError[0], '\0') << held->conditionError;
    EXPECT_GT(held->hitCount, 0u);
}

TEST(GSCardTarget_Test, AFalseConditionCountsMatchesWithoutStopping)
{
    Card card;
    card.boot();
    BreakpointSpec spec;
    spec.type = BRK_IO;
    spec.access = BRK_IO_IN;
    spec.address = 0x04;
    spec.portMask = 0x00FF;
    spec.condition = "IN == 4 && M(#026E) == 00";  // the ROM holds DB there
    const uint16_t id = card.add(spec);
    EXPECT_FALSE(card.runUntilStop(1));
    const BreakpointSnapshot held = card.bp().GetBreakpointById(id);
    ASSERT_TRUE(held);
    EXPECT_GT(held->matchCount, 0u) << "the poll reads port 4";
    EXPECT_EQ(held->hitCount, 0u);
    EXPECT_EQ(held->conditionError[0], '\0') << held->conditionError;
}

TEST(GSCardTarget_Test, AConditionRefusesTheMachinesState)
{
    Card card;
    for (const char* text : {"FD == 10", "DOS == 1", "T > 100"})
    {
        std::string error;
        BreakpointSpec spec = Exec(kIdleLoop);
        spec.condition = text;
        EXPECT_EQ(card.bp().AddBreakpoint(spec, error), BRK_INVALID) << text;
        EXPECT_NE(error.find("belongs to the main machine"), std::string::npos) << error;
    }
    std::string error;
    BreakpointSpec spec = Exec(kIdleLoop);
    spec.condition = "FRAME >= 0 && PG1 == 0";  // the machine's frame; the card's RAM page 0 at #4000
    EXPECT_NE(card.bp().AddBreakpoint(spec, error), BRK_INVALID) << error;
}

namespace
{
struct NeoCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;
    std::unique_ptr<GSCardTarget> target;

    NeoCard()
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
        chip->loadROM("rom/neogs/full_ngs.rom");
        target = std::make_unique<GSCardTarget>(&ctx, [this]() -> GeneralSoundCard* { return chip.get(); });
    }
    ~NeoCard() { target.reset(); }

    uint16_t add(const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = target->GetBreakpointManager()->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }

    bool runUntilStop(int frames)
    {
        const uint64_t before = target->StopCount();
        for (int f = 0; f < frames && target->StopCount() == before; f++)
            chip->runFor(2396020);  // a Pentagon frame of 120 MHz ticks
        return target->StopCount() != before;
    }
};
}  // namespace

// The same target on the NeoGS: the flash loader at #0000 is ROM page 0, its page register writes are card events
TEST(GSCardTarget_Test, TheNeoGSStopsOnItsLoaderAndItsPageWrites)
{
    NeoCard card;
    const MemoryPageDescriptor zero = card.target->MapAddress(0x0000);
    EXPECT_EQ(zero.mode, BANK_ROM) << "window 0 shows the flash after reset";
    EXPECT_EQ(zero.page, 0);
    EXPECT_EQ(card.target->PageCount(BANK_ROM), 32u);  // 512 KB flash

    BreakpointSpec spec = Exec(0x0000);
    spec.hasPage = true;
    spec.pageType = BANK_ROM;
    spec.page = 0;
    const uint16_t loader = card.add(spec);
    ASSERT_TRUE(card.runUntilStop(20));
    EXPECT_EQ(card.target->LastStop().breakpointId, loader);
    EXPECT_EQ(card.target->LastStop().boundaryPc, 0x0000);
    card.target->GetBreakpointManager()->RemoveBreakpointByID(loader);

    const uint16_t page = card.add(Event(GSCardEvent::PageSwitch));
    ASSERT_TRUE(card.runUntilStop(20));
    EXPECT_EQ(card.target->LastStop().breakpointId, page);
    EXPECT_EQ(card.target->LastStop().kind, BreakpointHitKind::Event);
}

// The card hardware's events: the classic card's DAC fetches (its POST plays through the DACs), the NeoGS loader's
// clock switch (GSCFG0)
TEST(GSCardTarget_Test, TheDacFetchAndTheClockChangeAreCardEvents)
{
    Card card;
    const uint16_t dac = card.add(Event(GSCardEvent::DacFetch));
    ASSERT_TRUE(card.runUntilStop(200)) << "dac fetches " << card.chip->getActivityCounters().dacFetches;
    EXPECT_EQ(card.target->LastStop().breakpointId, dac);
    EXPECT_EQ(card.target->LastStop().address, static_cast<uint16_t>(GSCardEvent::DacFetch));

    NeoCard neo;
    const uint16_t clock = neo.add(Event(GSCardEvent::ClockChange));
    ASSERT_TRUE(neo.runUntilStop(50));
    EXPECT_EQ(neo.target->LastStop().breakpointId, clock);
    EXPECT_EQ(neo.target->LastStop().value, neo.chip->gscfg0()) << "the byte is GSCFG0";
    neo.target->GetBreakpointManager()->RemoveBreakpointByID(clock);

    // Every GSCFG0 write of the card, a clock change or not (platform-debugging P2)
    const uint16_t config = neo.add(Event(GSCardEvent::ConfigWrite));
    ASSERT_TRUE(neo.runUntilStop(50));
    EXPECT_EQ(neo.target->LastStop().breakpointId, config);
    EXPECT_EQ(neo.target->LastStop().address, static_cast<uint16_t>(GSCardEvent::ConfigWrite));
    EXPECT_EQ(neo.target->LastStop().value, neo.chip->gscfg0());
}

// A NeoGS DMA module's RAM write (platform-debugging P2) reaches a write watchpoint tied to the RAM page, not one on
// the card CPU's address: the stop is a dma_write with the module, the page and the offset in it
TEST(GSCardTarget_Test, TheNeoGSDmaWritesHitThePagesWatchpoint)
{
    NeoCard card;
    constexpr uint8_t kPage = 4;
    constexpr uint16_t kOffset = 0x2005;
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_WRITE;
    spec.address = kOffset;
    spec.hasPage = true;
    spec.pageType = BANK_RAM;
    spec.page = kPage;
    const uint16_t id = card.add(spec);
    ASSERT_TRUE(card.target->HooksAttached());
    NeoGSZxDma::Host& zx = card.chip->zxHost();
    NeoGSDma::Host& sd = card.chip->dmaHost();
    constexpr uint32_t kAt = kPage * NeoGSMemory::PAGE_SIZE + kOffset;

    zx.zxRamWritten(kAt + 1, 0x11);                            // the next byte
    zx.zxRamWritten((kPage + 1) * NeoGSMemory::PAGE_SIZE + kOffset, 0x22);  // the next page
    EXPECT_FALSE(card.runUntilStop(1)) << "only the watched byte of the watched page";

    zx.zxRamWritten(kAt, 0x5A);
    ASSERT_TRUE(card.runUntilStop(1));
    GSCardTarget::Stop stop = card.target->LastStop();
    EXPECT_EQ(stop.breakpointId, id);
    EXPECT_EQ(stop.kind, BreakpointHitKind::DmaWrite);
    EXPECT_STREQ(BreakpointHitKindName(stop.kind), "dma_write");
    EXPECT_EQ(stop.master, NeoGSDma::ZX);
    EXPECT_EQ(stop.page, kPage);
    EXPECT_EQ(stop.address, kOffset);
    EXPECT_EQ(stop.value, 0x5A);

    // An SD block over the byte: the byte it left there
    uint8_t* ram = card.chip->memory().ram();
    for (uint32_t i = 0; i < 512; i++)
        ram[kAt - 0x100 + i] = static_cast<uint8_t>(i);
    sd.dmaRamWritten(kAt - 0x100, 512);
    ASSERT_TRUE(card.runUntilStop(1));
    stop = card.target->LastStop();
    EXPECT_EQ(stop.master, NeoGSDma::SD);
    EXPECT_EQ(stop.address, kOffset);
    EXPECT_EQ(stop.value, 0x00) << "byte #100 of the block";
}

// The card of a whole machine (the PENTAGON's NeoGS): a card breakpoint puts the hooks on the card the machine runs,
// and its stop ends the main CPU's direct run. Without an SD card the loader polls it with interrupts off: the
// instruction the card is at comes round again
// The card CPU's labels (gs-debugger design §6.2, L1-L4): its own store, the shipped firmware symbols chosen by the
// SHA-256 of the card's ROM page 0, ROM labels tied to their pages; the card's listing names its instructions and
// operands from them, the main CPU's labels stay out
// A card fitted with the machine (the PENTAGON's own NeoGS, no switch afterwards) has its firmware symbols from the
// start: the debug manager comes after the card, so the machine's bundle pass applies the card's too
TEST(GSCardTarget_Test, TheCardFittedWithTheMachineHasItsSymbols)
{
    SoundCardScope sound{TestSound::GeneralSound};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    GSCardTarget* target = emulator->GetContext()->pDebugManager->GetCardTarget(DebugCpuId::GS);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(target->Card(), nullptr) << "the PENTAGON fits a General Sound";
    bool bundle = false;
    for (const auto& set : target->GetLabelManager()->GetSymbolSets())
        bundle = bundle || set.id.rfind("bundle:gs:", 0) == 0;
    EXPECT_TRUE(bundle) << "the card's firmware symbols without a card switch";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(GSCardTarget_Test, TheFirmwareSymbolsNameTheCardsCode)
{
    Card card;
    const std::vector<std::string> sets = card.target->ApplySymbolBundles();
    ASSERT_EQ(sets, std::vector<std::string>{"bundle:gs:gs105a"});
    LabelManager& labels = *card.target->GetLabelManager();
    EXPECT_EQ(labels.Cpu(), "gs");

    const auto comint = labels.GetLabelByName("COMINT");
    ASSERT_NE(comint, nullptr);
    EXPECT_EQ(comint->address, 0x026B);
    EXPECT_TRUE(comint->isROM());
    EXPECT_EQ(comint->bank, 0);
    // The high ROM's command table: ROM page 1 at #C000 (page 0 of the 32K window)
    const auto comtabh = labels.GetLabelByName("COMTABH");
    ASSERT_NE(comtabh, nullptr);
    EXPECT_EQ(comtabh->address, 0xD720);
    EXPECT_TRUE(comtabh->isROM());
    EXPECT_EQ(comtabh->bank, 1);
    ASSERT_NE(labels.GetLabelByName("BUSY"), nullptr);  // a firmware variable in the fixed RAM window
    EXPECT_EQ(labels.GetLabelByName("BUSY")->address, 0x4086);

    // A ROM 1 label and a RAM label at one address: the page the window shows decides
    ASSERT_TRUE(labels.AddLabel("RamThere", 0xD720, 2, 0x1720, "data"));
    std::shared_ptr<Label> at = labels.GetLabelByZ80Address(0xD720);
    ASSERT_NE(at, nullptr);
    const GeneralSoundCard::CardPage window3 = card.chip->mapCardAddress(0xD720);
    EXPECT_EQ(at->name, window3.kind == GeneralSoundCard::CardPage::Rom && window3.page == 1 ? "COMTABH" : "RamThere");
    labels.RemoveLabel("RamThere");

    // The listing (no machine, so the disassembler directly): the instruction's label from the card's store
    Z80Disassembler disassembler(&card.ctx);
    Z80Disassembler::LabelsScope scope(&labels);
    std::vector<uint8_t> bytes(4);
    for (uint16_t i = 0; i < 4; i++)
        ASSERT_TRUE(card.chip->peekCardMemory(static_cast<uint16_t>(0x026B + i), bytes[i]));
    uint8_t length = 0;
    DecodedInstruction decoded;
    disassembler.disassembleSingleCommand(bytes, 0x026B, &length, &decoded);
    EXPECT_EQ(decoded.label, "COMINT");
}

// The card a machine is built with (the PENTAGON's NeoGS) has its firmware's symbols from the start, with no switch of
// the card: the set the GS report names (firmware_profile.symbols) is in the card CPU's store
TEST(GSCardTarget_Test, TheCardAMachineIsBuiltWithHasItsFirmwareSymbols)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    GeneralSoundCard* card = context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->hasCoprocessor());
    const GSFirmwareIdentity identity = IdentifyGSFirmware(*card);
    ASSERT_NE(identity.profile, nullptr) << "the shipped flash has a profile";
    const std::string wanted = std::string("bundle:gs:") + identity.profile->id;
    LabelManager* labels = context->pDebugManager->GetLabelManager(DebugCpuId::GS);
    ASSERT_NE(labels, nullptr);
    bool found = false;
    for (const auto& set : labels->GetSymbolSets())
        found = found || set.id == wanted;
    EXPECT_TRUE(found) << wanted;
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// The machine's card takes its firmware's symbols when it is fitted; the main CPU's labels do not get them
TEST(GSCardTarget_Test, TheMachinesCardLoadsItsFirmwareSymbols)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    LabelManager* labels = context->pDebugManager->GetLabelManager(DebugCpuId::GS);
    ASSERT_NE(labels, nullptr);
    EXPECT_NE(labels, context->pDebugManager->GetLabelManager());
    ASSERT_NE(labels->GetLabelByName("COMINT"), nullptr);
    EXPECT_EQ(context->pDebugManager->GetLabelManager()->GetLabelByName("COMINT"), nullptr);

    // The card's listing at COMINT carries its name
    StateNode listing = CpuView::Disasm(context, DebugCpuId::GS, 0x026B, 1);
    ASSERT_EQ(listing["instructions"].size(), 1u);
    EXPECT_EQ(listing["instructions"].items[0]["label"].s, "COMINT");
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(GSCardTarget_Test, TheMachinesCardStopsWhereItLoops)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    GSCardTarget* target = context->pDebugManager->GetGSTarget();
    ASSERT_NE(target, nullptr);
    emulator->RunNFrames(2);

    Z80CpuRegisters regs{};
    ASSERT_TRUE(context->pSoundManager->getGeneralSound()->cardRegisters(regs));
    std::string error;
    const uint16_t id =
        context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS)->AddBreakpoint(Exec(regs.pc), error);
    ASSERT_NE(id, BRK_INVALID) << error;
    EXPECT_TRUE(target->HooksAttached());

    emulator->RunNFrames(1, false);
    EXPECT_GT(target->StopCount(), 0u);
    EXPECT_EQ(target->LastStop().breakpointId, id);
    EXPECT_EQ(target->LastStop().boundaryPc, regs.pc);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Tight mode (gs-debugger design §5.2): a stopping card breakpoint or an armed card step has the main CPU catch the
// card up at every instruction start, so the card's stop comes while main is within an instruction of it, and the pause
// says where main stands. Without it the card runs at port accesses and frame ends only: main is found up to a frame
// ahead. Boot-bound, turbo: about 100 ms for both runs
TEST(GSCardTarget_Test, ACardBreakpointParksTheMainCpuNearby)
{
    auto stopWith = [](bool tight) -> MainPosition {
        MainPosition main;
        SoundCardScope sound(TestSound::GeneralSound);
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return main;
        EmulatorContext* context = emulator->GetContext();
        EXPECT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
        FeatureManager* features = emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        GSCardTarget* target = context->pDebugManager->GetGSTarget();
        GeneralSoundCard* card = context->pSoundManager->getGeneralSound();
        // Stopped at a frame's end, the card caught up to it (a pause mid-frame would leave it lazily behind)
        emulator->RunNFrames(3);
        EXPECT_FALSE(features->cardTightHold());

        Z80CpuRegisters regs{};
        EXPECT_TRUE(card->cardRegisters(regs));
        std::string error;
        BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
        const uint16_t id = breakpoints->AddBreakpoint(Exec(regs.pc), error);
        EXPECT_NE(id, BRK_INVALID) << error;
        EXPECT_TRUE(features->cardTightHold()) << "a stopping card breakpoint";
        if (!tight)
            features->setCardTightHold(false);

        const uint64_t stops = target->StopCount();
        emulator->EnableTurboMode();
        emulator->StartAsync();
        EXPECT_TRUE(TestWait::For([&] { return target->StopCount() > stops && emulator->IsParkedInsideCardRun(); }));
        main = emulator->LastStop().breakpoint.main;
        EXPECT_TRUE(main.known);
        EXPECT_EQ(main.gapT, static_cast<int32_t>(std::llround(
                                 static_cast<double>(card->hostLead()) /
                                 GSHostClock::unitsPerZxTact(context, card->cardUnitsPerSecond()))))
            << "the gap as the parked card has it";
        EXPECT_EQ(main.pc, context->pCore->GetZ80()->m1_pc);

        breakpoints->RemoveBreakpointByID(id);
        EXPECT_FALSE(features->cardTightHold()) << "no card breakpoint left";
        EXPECT_EQ(target->ArmStep(GSCardTarget::StepKind::In), "");
        EXPECT_TRUE(features->cardTightHold()) << "an armed card step";
        target->CancelStep();
        EXPECT_FALSE(features->cardTightHold());
        emulator->Stop();
        EmulatorTestHelper::CleanupEmulator(emulator);
        return main;
    };
    const MainPosition tight = stopWith(true);
    EXPECT_FALSE(tight.frameEnd);
    EXPECT_GE(tight.gapT, 0);
    EXPECT_LT(tight.gapT, 24) << "main within an instruction of the card's stop";
    // The card caught up at main's instruction start: main finished the instruction the card's moment is in, and the
    // pause says how far into it that moment is and which bus cycle it was (design §5.4)
    EXPECT_TRUE(tight.onBoundary);
    EXPECT_TRUE(tight.timed);
    EXPECT_EQ(static_cast<int64_t>(tight.elapsedT), static_cast<int64_t>(tight.tInInstruction) - tight.gapT);
    EXPECT_GT(tight.totalT, 0);
    EXPECT_GT(tight.cycles.count, 0);
    EXPECT_EQ(tight.cycles.total, tight.totalT);
    EXPECT_EQ(tight.totalTAlt, 0) << "finished: the way taken is known";
    if (!tight.contended)
        EXPECT_EQ(tight.tInInstruction, tight.totalT) << "Pentagon: no contention, all of the instruction's T";
    else
        EXPECT_GT(tight.tInInstruction, tight.totalT) << "an interrupt's acknowledge after it";
    if (tight.elapsedT < tight.totalT)
        EXPECT_GE(tight.busCycle, 0);
    const MainPosition lazy = stopWith(false);
    EXPECT_TRUE(lazy.frameEnd || lazy.gapT > 1000) << "lazy: main ran on to a port access or the frame end; gap "
                                                   << lazy.gapT;
}

// The card a front-end shows follows the main CPU's steps of the paused machine (gs-debugger design §5.2, the third
// trigger): tight mode for the direct run and a catch-up at its end, so the card stands at the main CPU's moment, not
// at the last port access or frame end. Tight mode is off again after the run, and off while the main CPU is shown
TEST(GSCardTarget_Test, TheSelectedCardFollowsTheMainCpusSteps)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    GeneralSoundCard* card = context->pSoundManager->getGeneralSound();
    DebugManager* debug = context->pDebugManager;
    emulator->RunNFrames(3);
    // The card's lag behind the main CPU in ZX T (the card can end up to an instruction ahead: negative)
    auto gap = [&] {
        return std::llround(static_cast<double>(card->hostLead()) /
                            GSHostClock::unitsPerZxTact(context, card->cardUnitsPerSecond()));
    };

    emulator->RunNCPUCycles(40, true);
    const long long lazy = gap();
    EXPECT_GT(lazy, 100) << "not shown: the card stays where the frame end left it";

    EXPECT_EQ(CpuView::SetSelected(context, "gs"), "");
    EXPECT_EQ(debug->SelectedCpu(), DebugCpuId::GS);
    EXPECT_FALSE(features->cardTightHold()) << "shown, but nothing runs: no tight mode";
    for (int i = 0; i < 5; i++)
    {
        emulator->RunSingleCPUCycle(true);
        EXPECT_LT(std::llabs(gap()), 24) << "step " << i << ": the card at the main CPU's moment";
        EXPECT_FALSE(features->cardTightHold()) << "off again after the step";
    }
    emulator->RunNCPUCycles(40, true);
    EXPECT_LT(std::llabs(gap()), 24) << "after steps too";
    StateNode selected = CpuView::Selected(context);
    EXPECT_EQ(selected["cpu"].s, "gs");
    EXPECT_TRUE(selected["follows"].b);

    EXPECT_EQ(CpuView::SetSelected(context, "main"), "");
    emulator->RunNCPUCycles(40, true);
    EXPECT_GT(gap(), 100) << "the main CPU shown: the card lags again";
    EXPECT_FALSE(CpuView::Selected(context)["follows"].b);
    EXPECT_NE(CpuView::SetSelected(context, "ms"), "") << "no MultiSound card";
    EXPECT_NE(CpuView::SetSelected(context, "zx"), "");
    EXPECT_EQ(debug->SelectedCpu(), DebugCpuId::Main) << "a refused choice keeps the last one";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Tight mode only splits the card's catch-up into more runs, it does not change them (gs-debugger design §5.6): the
// machine's NeoGS boots through its loader's clock switch with the same card-side events lazy and tight - every port
// access of either side, DAC fetch and INT at the same card time, from the same PC, with the same byte. A breakpoint
// never hit keeps the card in its debug loop both ways. (The card's time after a frame is not compared: tight, the
// card follows the main CPU's first T of the next frame.) About 0.4 s
TEST(GSCardTarget_Test, TightModeRunsTheCardAsLazyDoes)
{
    constexpr size_t kCapacity = size_t{1} << 18;
    struct Run
    {
        std::vector<GSTraceEvent> events;
        uint8_t clockBefore = 0;
        uint8_t clockAfter = 0;
    };
    auto run = [](bool tight) -> Run {
        Run result;
        SoundCardScope sound(TestSound::GeneralSound);
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return result;
        EmulatorContext* context = emulator->GetContext();
        EXPECT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
        FeatureManager* features = emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        auto* card = dynamic_cast<SoundChip_NeoGS*>(context->pSoundManager->getGeneralSound());
        EXPECT_NE(card, nullptr);
        if (!card)
            return result;
        std::string error;
        BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
        EXPECT_NE(breakpoints->AddBreakpoint(Exec(0x3FFF), error), BRK_INVALID) << error;  // never executed
        features->setCardTightHold(tight);

        GSHistory events{"gs", "GS port and activity events", kCapacity};
        EXPECT_TRUE(events.Configure({.mode = history::Mode::Ring, .limit = kCapacity}, error)) << error;
        card->attachPortTrace(&events);
        result.clockBefore = card->gscfg0();
        emulator->RunNFrames(25);
        result.clockAfter = card->gscfg0();
        card->attachPortTrace(nullptr);
        result.events = events.Snapshot();
        EmulatorTestHelper::CleanupEmulator(emulator);
        return result;
    };
    const Run lazy = run(false);
    const Run tight = run(true);
    ASSERT_FALSE(lazy.events.empty());
    ASSERT_FALSE(tight.events.empty());
    EXPECT_LT(lazy.events.size(), kCapacity) << "the whole run in the ring";
    EXPECT_LT(tight.events.size(), kCapacity);
    EXPECT_NE(lazy.clockBefore, lazy.clockAfter) << "the loader switched the clock within the run";
    EXPECT_EQ(tight.clockAfter, lazy.clockAfter);

    // Up to where both got: the later of the two runs' ends is past the other's last event
    const int64_t end = std::min(lazy.events.back().timestamp, tight.events.back().timestamp);
    size_t compared = 0;
    for (size_t i = 0; i < lazy.events.size() && lazy.events[i].timestamp <= end; i++, compared++)
    {
        ASSERT_LT(i, tight.events.size());
        const GSTraceEvent& l = lazy.events[i];
        const GSTraceEvent& t = tight.events[i];
        ASSERT_TRUE(l.timestamp == t.timestamp && l.side == t.side && l.port == t.port && l.pc == t.pc &&
                    l.value == t.value && l.flags == t.flags && l.channel == t.channel)
            << "event " << i << ": lazy t=" << l.timestamp << " port #" << std::hex << l.port << " pc #" << l.pc
            << " = #" << int(l.value) << ", tight t=" << std::dec << t.timestamp << " port #" << std::hex << t.port
            << " pc #" << t.pc << " = #" << int(t.value);
    }
    EXPECT_GT(compared, 100000u);
    EXPECT_TRUE(compared == tight.events.size() || tight.events[compared].timestamp > end);
}

/// The MultiSound's General Sound is the `ms` CPU (platform-debugging P1): its own target and breakpoints, found on
/// the slot card; the General Sound slot's target stays empty. An execute breakpoint where its firmware loops stops
/// there; the stop names the CPU
TEST(GSCardTarget_Test, TheMultiSoundsGeneralSoundIsTheMsCpu)
{
    multisoundtest::StagedMachine m("pentagon128k", "zxbus.1 = multisound\nzxbus.1.dip = ym,saa,gs");
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    m.Machine().GetFeatureManager()->setFeature(Features::kDebugMode, true);
    DebugManager* debug = m.Context()->pDebugManager;
    GSCardTarget* target = debug->GetCardTarget(DebugCpuId::MultiSoundGS);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Card(), card->DebugGeneralSound());
    EXPECT_STREQ(target->Name(), "ms");
    EXPECT_EQ(target->Id(), DebugCpuId::MultiSoundGS);
    DebugCpuId parsed = DebugCpuId::Main;
    EXPECT_TRUE(DebugManager::ParseCpuName("ms", parsed));
    EXPECT_EQ(parsed, DebugCpuId::MultiSoundGS);
    m.Machine().RunNFrames(2);

    Z80CpuRegisters regs{};
    ASSERT_TRUE(card->Card().Gs().cardRegisters(regs));
    std::string error;
    const uint16_t id = debug->GetBreakpointsManager(DebugCpuId::MultiSoundGS)->AddBreakpoint(Exec(regs.pc), error);
    ASSERT_NE(id, BRK_INVALID) << error;
    EXPECT_TRUE(target->HooksAttached());
    EXPECT_FALSE(debug->GetGSTarget()->HooksAttached()) << "the General Sound slot's target is another";
    EXPECT_EQ(debug->GetBreakpointsManager(DebugCpuId::GS)->GetBreakpointsCount(), 0u);

    m.Machine().RunNFrames(1, false);
    EXPECT_GT(target->StopCount(), 0u);
    EXPECT_EQ(target->LastStop().breakpointId, id);
    EXPECT_EQ(target->LastStop().boundaryPc, regs.pc);
}

namespace
{
// A routine in the card's RAM at #4000 (the classic card's RAM page 1, the NeoGS's RAM page 3), interrupts off:
//   #4000 CALL #4010 / #4003 NOP / #4004 JR #4000 ... #4010 NOP / #4011 NOP / #4012 RET Z / #4013 RET
constexpr uint16_t kCall = 0x4000;
constexpr uint16_t kAfterCall = 0x4003;
constexpr uint16_t kLoop = 0x4004;
constexpr uint16_t kRoutine = 0x4010;
constexpr uint16_t kStack = 0x7F00;

void LoadRoutine(GeneralSoundCard& card)
{
    const uint8_t caller[] = {0xCD, 0x10, 0x40, 0x00, 0x18, 0xFA};
    const uint8_t routine[] = {0x00, 0x00, 0xC8, 0xC9};
    for (size_t i = 0; i < sizeof(caller); i++)
        ASSERT_TRUE(card.pokeCardMemory(static_cast<uint16_t>(kCall + i), caller[i]));
    for (size_t i = 0; i < sizeof(routine); i++)
        ASSERT_TRUE(card.pokeCardMemory(static_cast<uint16_t>(kRoutine + i), routine[i]));
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card.cardRegisters(regs));
    regs.pc = kCall;
    regs.sp = kStack;
    regs.af = 0x0000;  // Z clear: RET Z falls through
    regs.iff1 = regs.iff2 = 0;
    regs.halted = 0;
    ASSERT_TRUE(card.setCardRegisters(regs));
}

uint16_t CardSp(GeneralSoundCard& card)
{
    Z80CpuRegisters regs{};
    card.cardRegisters(regs);
    return regs.sp;
}
}  // namespace

// A step parks at an instruction boundary (gs-debugger design §5.3). Without a machine the card runs on past the
// park, so each step starts from the routine's entry: in after one instruction, in 3 after three, over at the
// instruction after the CALL with the entry SP, out after the taken RET (RET Z not taken does not end it), to at the
// cursor (a cursor at PC: the instruction's next run, once round the loop)
TEST(GSCardTarget_Test, AStepParksAtItsBoundary)
{
    Card card;
    card.boot();
    struct Case
    {
        GSCardTarget::StepKind kind;
        uint32_t count;
        uint16_t start;
        uint16_t boundary;
    };
    for (const Case& c : {Case{GSCardTarget::StepKind::In, 1, kCall, kRoutine},
                          Case{GSCardTarget::StepKind::In, 3, kCall, kRoutine + 2},
                          Case{GSCardTarget::StepKind::Over, 1, kCall, kAfterCall},
                          Case{GSCardTarget::StepKind::Over, 1, kAfterCall, kLoop},
                          Case{GSCardTarget::StepKind::Out, 1, kRoutine, kAfterCall},
                          Case{GSCardTarget::StepKind::To, 1, kCall, kRoutine + 3},
                          Case{GSCardTarget::StepKind::To, 1, kCall, kCall}})
    {
        LoadRoutine(*card.chip);
        Z80CpuRegisters regs{};
        card.chip->cardRegisters(regs);
        regs.pc = c.start;
        if (c.start == kRoutine)
        {
            // Inside the routine as the CALL left it: the return address on the stack
            regs.sp = kStack - 2;
            card.chip->pokeCardMemory(kStack - 2, kAfterCall & 0xFF);
            card.chip->pokeCardMemory(kStack - 1, kAfterCall >> 8);
        }
        card.chip->setCardRegisters(regs);
        ASSERT_EQ(card.target->ArmStep(c.kind, c.count, c.boundary), "");  // the cursor for To
        EXPECT_TRUE(card.target->HooksAttached()) << "an armed step runs the debug loop";
        ASSERT_TRUE(card.runUntilStop(1)) << "start #" << std::hex << c.start;
        const GSCardTarget::Stop stop = card.target->LastStop();
        EXPECT_TRUE(stop.step);
        EXPECT_FALSE(stop.hit);
        EXPECT_EQ(stop.boundaryPc, c.boundary) << "start #" << std::hex << c.start;
        EXPECT_FALSE(card.target->StepArmed());
        card.frame();
        EXPECT_FALSE(card.target->HooksAttached()) << "the step done, the hooks come off";
    }
}

// A breakpoint on the way ends a step there: the stop is the breakpoint's, the step is disarmed
TEST(GSCardTarget_Test, ABreakpointEndsAStepOver)
{
    Card card;
    card.boot();
    LoadRoutine(*card.chip);
    const uint16_t id = card.add(Exec(kRoutine + 1));
    ASSERT_EQ(card.target->ArmStep(GSCardTarget::StepKind::Over), "");
    EXPECT_NE(card.target->ArmStep(GSCardTarget::StepKind::In), "") << "one step at a time";
    ASSERT_TRUE(card.runUntilStop(1));
    EXPECT_TRUE(card.target->LastStop().hit);
    EXPECT_EQ(card.target->LastStop().breakpointId, id);
    EXPECT_EQ(card.target->LastStop().boundaryPc, kRoutine + 1);
    EXPECT_FALSE(card.target->StepArmed());
}

// The machine's card (the PENTAGON's NeoGS) through RunControl::CardStep as every surface calls it: the machine
// resumes, the card's step pauses it, the reply names the card CPU's stop and position. Parked inside the card's run,
// the next step starts from that boundary. Boot-bound, turbo: about 50 ms
TEST(GSCardTarget_Test, TheMachinesCardStepsThroughRunControl)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->EnableTurboMode();
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter > 2; }));
    emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsEmulationParked(); }));
    GeneralSoundCard* card = context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    LoadRoutine(*card);

    std::shared_ptr<Emulator> shared = EmulatorManager::GetInstance()->GetEmulator(emulator->GetId());
    ASSERT_NE(shared, nullptr);
    execution::RunControl control(shared);
    const execution::Requester requester{execution::Origin::WebApi, {}};
    auto step = [&](const char* kind, unsigned count, const char* reason, uint16_t pc, uint16_t cursor = 0) {
        const execution::RunReply reply = control.CardStep(requester, DebugCpuId::GS, kind, count, cursor);
        EXPECT_TRUE(reply.Ok()) << kind << ": " << reply.message;
        const StateNode* stop = reply.body.find("stop");
        EXPECT_NE(stop, nullptr) << kind;
        if (!stop)
            return;
        EXPECT_EQ(stop->find("reason")->s, reason) << kind << ": " << reply.message;
        EXPECT_EQ(stop->find("cpu")->s, "gs") << kind;
        EXPECT_EQ(reply.body.find("pc")->i, pc) << kind;
        EXPECT_NE(reply.body.find("main_pc"), nullptr);
        // The card never runs ahead of main (design §5.3): its stop has main at or past it. How far: as far as the
        // card was behind when the machine paused (between frames none; a pause mid-frame leaves it lazily behind)
        const StateNode* main = stop->find("main");
        EXPECT_NE(main, nullptr) << kind;
        if (main)
            EXPECT_GE(main->find("gap_t")->i, 0) << kind;
        EXPECT_EQ(reply.body.find("state")->s, "paused") << kind;
        EXPECT_TRUE(emulator->IsPaused());
        if (std::string(kind) == "to")
            EXPECT_EQ(reply.body.find("hit")->b, std::string(reason) == "step") << "to #" << std::hex << cursor;
    };
    step("in", 1, "step", kRoutine);  // paused between frames: the first boundary is the CALL's own
    step("in", 2, "step", kRoutine + 2);
    step("out", 1, "step", kAfterCall);
    EXPECT_EQ(CardSp(*card), kStack);
    step("over", 1, "step", kLoop);  // a NOP: as in
    step("in", 1, "step", kCall);

    std::string error;
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddBreakpoint(Exec(kRoutine + 1), error);
    ASSERT_NE(id, BRK_INVALID) << error;
    step("over", 1, "breakpoint", kRoutine + 1);
    breakpoints->RemoveBreakpointByID(id);
    step("out", 1, "step", kAfterCall);

    // Run to the cursor: round the loop into the routine; a breakpoint on the way ends it first
    step("to", 1, "step", kRoutine + 1, kRoutine + 1);
    step("to", 1, "step", kRoutine + 1, kRoutine + 1);  // at the cursor: its next run
    const uint16_t before = breakpoints->AddBreakpoint(Exec(kCall), error);
    ASSERT_NE(before, BRK_INVALID) << error;
    step("to", 1, "breakpoint", kCall, kRoutine);
    breakpoints->RemoveBreakpointByID(before);

    const execution::RunReply refused = control.CardStep(requester, DebugCpuId::MultiSoundGS, "in", 1);
    EXPECT_FALSE(refused.Ok()) << "no MultiSound in this machine";
    EXPECT_EQ(control.CardStep(requester, DebugCpuId::GS, "sideways", 1).status,
              execution::RunReply::Status::BadRequest);
    emulator->Stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

namespace
{
/// The machine's NeoGS (PENTAGON), paused after a few turbo frames, the card in the routine at #4000; the main CPU
/// at a program of its own at #8000 (interrupts off): LD A,#23 / OUT (#BB),A / JR $ - one host command, then a loop
struct CardRunMachine
{
    static constexpr uint16_t kMainProgram = 0x8000;
    static constexpr uint16_t kMainOut = 0x8002;
    static constexpr uint16_t kMainLoop = 0x8004;

    SoundCardScope sound{TestSound::GeneralSound};
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    GeneralSoundCard* card = nullptr;
    std::shared_ptr<Emulator> shared;
    const execution::Requester requester{execution::Origin::WebApi, {}};

    bool Start()
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        if (!emulator)
            return false;
        context = emulator->GetContext();
        if (!FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS))
            return false;
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->EnableTurboMode();
        emulator->StartAsync();
        if (!TestWait::For([&] { return context->emulatorState.frame_counter > 2; }))
            return false;
        emulator->Pause();
        if (!TestWait::For([&] { return emulator->IsEmulationParked(); }))
            return false;
        card = context->pSoundManager->getGeneralSound();
        shared = EmulatorManager::GetInstance()->GetEmulator(emulator->GetId());
        return card && shared;
    }

    /// The main CPU at its program (the command written once it runs)
    void MainProgram()
    {
        const uint8_t program[] = {0x3E, 0x23, 0xD3, 0xBB, 0x18, 0xFE};
        for (size_t i = 0; i < sizeof(program); i++)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kMainProgram + i), program[i]);
        Z80State* z80 = emulator->GetZ80State();
        z80->pc = kMainProgram;
        z80->iff1 = z80->iff2 = 0;
    }

    execution::RunReply Run(const char* until, uint64_t value = 0, DebugCpuId cpu = DebugCpuId::GS)
    {
        return execution::RunControl(shared).CardRunTo(requester, cpu, until, value);
    }

    ~CardRunMachine()
    {
        if (emulator)
        {
            emulator->Stop();
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }
};

std::string Reason(const execution::RunReply& reply)
{
    const StateNode* stop = reply.body.find("stop");
    const StateNode* reason = stop ? stop->find("reason") : nullptr;
    return reason ? reason->s : std::string();
}
}  // namespace

// Run to a card cycle (gs-debugger TODO P5.4): the card parks at the first boundary at or after it, the reply says the
// cycle reached; a cycle already passed is the next frame's; a breakpoint on the way ends it first. Bad arguments are
// refused before anything runs. Boot-bound, turbo
TEST(GSCardRunTo_Test, ACardCycleIsReachedAndReported)
{
    CardRunMachine m;
    ASSERT_TRUE(m.Start());
    LoadRoutine(*m.card);
    const int64_t frameCycles = m.card->cardCycles(m.card->cardFrameLength());
    ASSERT_GT(frameCycles, 10000);

    // From where the card stands (a step first: the card at a boundary, its cycle in the reply)
    execution::RunReply step = execution::RunControl(m.shared).CardStep(m.requester, DebugCpuId::GS, "in", 1);
    ASSERT_TRUE(step.Ok()) << step.message;
    ASSERT_NE(step.body.find("card_cycle"), nullptr);
    const int64_t at = step.body["card_cycle"].i;
    const int64_t frameBefore = step.body["frame"].i;
    const bool wraps = at + 3000 >= frameCycles;
    const int64_t ahead = wraps ? 5000 : at + 3000;

    execution::RunReply reached = m.Run("cycle", static_cast<uint64_t>(ahead));
    ASSERT_TRUE(reached.Ok()) << reached.message;
    EXPECT_EQ(Reason(reached), "step") << reached.message;
    EXPECT_TRUE(reached.body["hit"].b);
    EXPECT_EQ(reached.body["until"].s, "cycle");
    EXPECT_EQ(reached.body["target_cycle"].i, ahead);
    EXPECT_EQ(reached.body["stop"]["cpu"].s, "gs");
    const int64_t got = reached.body["card_cycle"].i;
    EXPECT_GE(got, ahead) << "the first boundary at or after the cycle";
    EXPECT_LT(got, ahead + 200) << "one card instruction past it at most";
    EXPECT_NE(reached.message.find("stopped at card cycle " + std::to_string(got)), std::string::npos)
        << reached.message;
    EXPECT_TRUE(m.emulator->IsPaused());
    if (!wraps)
        EXPECT_EQ(reached.body["frame"].i, frameBefore);

    // Already past in this frame: the next frame's
    const int64_t behind = got / 2;
    execution::RunReply next = m.Run("cycle", static_cast<uint64_t>(behind));
    ASSERT_TRUE(next.Ok()) << next.message;
    EXPECT_TRUE(next.body["hit"].b) << next.message;
    EXPECT_GE(next.body["card_cycle"].i, behind);
    EXPECT_LT(next.body["card_cycle"].i, behind + 200);
    EXPECT_GT(next.body["frame"].i, reached.body["frame"].i) << "the next frame's";

    // A breakpoint on the way ends it first
    std::string error;
    BreakpointManager* breakpoints = m.context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddBreakpoint(Exec(kRoutine + 1), error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const int64_t farCycle = (next.body["card_cycle"].i + frameCycles / 2) % frameCycles;
    execution::RunReply stopped = m.Run("cycle", static_cast<uint64_t>(farCycle));
    ASSERT_TRUE(stopped.Ok()) << stopped.message;
    EXPECT_EQ(Reason(stopped), "breakpoint") << stopped.message;
    EXPECT_FALSE(stopped.body["hit"].b);
    EXPECT_EQ(stopped.body["pc"].i, kRoutine + 1);
    breakpoints->RemoveBreakpointByID(id);
    EXPECT_FALSE(m.context->pDebugManager->GetGSTarget()->StepArmed()) << "nothing left armed";

    // Refused: past the frame, an unknown run, the main CPU, no MultiSound
    EXPECT_EQ(m.Run("cycle", static_cast<uint64_t>(frameCycles)).status, execution::RunReply::Status::BadRequest);
    EXPECT_EQ(m.Run("sideways").status, execution::RunReply::Status::BadRequest);
    EXPECT_EQ(m.Run("int", 0, DebugCpuId::Main).status, execution::RunReply::Status::BadRequest);
    EXPECT_FALSE(m.Run("int", 0, DebugCpuId::MultiSoundGS).Ok());
    EXPECT_TRUE(m.emulator->IsPaused());
}

// Run to the card's next interrupt: parked at the handler's first instruction (IM 1: #0038), as an `int` event
// breakpoint parks; with interrupts off a breakpoint on the way ends it
TEST(GSCardRunTo_Test, TheNextCardInterruptParksAtItsHandler)
{
    CardRunMachine m;
    ASSERT_TRUE(m.Start());
    LoadRoutine(*m.card);
    Z80CpuRegisters regs{};
    ASSERT_TRUE(m.card->cardRegisters(regs));
    regs.iff1 = regs.iff2 = 1;
    regs.im = 1;
    ASSERT_TRUE(m.card->setCardRegisters(regs));

    execution::RunReply reply = m.Run("int");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Reason(reply), "step") << reply.message;
    EXPECT_TRUE(reply.body["hit"].b);
    EXPECT_EQ(reply.body["pc"].i, 0x0038) << reply.message;
    EXPECT_EQ(reply.body["stop"]["cpu_pc"].i, 0x0038);
    EXPECT_NE(reply.message.find("accepted an interrupt"), std::string::npos) << reply.message;
    ASSERT_TRUE(m.card->cardRegisters(regs));
    EXPECT_EQ(regs.iff1, 0) << "inside the handler";

    // Interrupts off: the loop's breakpoint ends it
    LoadRoutine(*m.card);
    std::string error;
    BreakpointManager* breakpoints = m.context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddBreakpoint(Exec(kRoutine), error);
    ASSERT_NE(id, BRK_INVALID) << error;
    execution::RunReply stopped = m.Run("int");
    ASSERT_TRUE(stopped.Ok()) << stopped.message;
    EXPECT_EQ(Reason(stopped), "breakpoint");
    EXPECT_FALSE(stopped.body["hit"].b);
    breakpoints->RemoveBreakpointByID(id);
}

// Run to the host's next command: the machine pauses after the main CPU's OUT (#BB), the reply names the byte, the card
// has not taken it yet (the command flip-flop up). A main breakpoint before the OUT ends it first
TEST(GSCardRunTo_Test, TheNextHostCommandPausesAfterTheOut)
{
    CardRunMachine m;
    ASSERT_TRUE(m.Start());
    LoadRoutine(*m.card);
    m.MainProgram();

    BreakpointManager* main = m.context->pDebugManager->GetBreakpointsManager();
    const uint16_t id = main->AddExecutionBreakpoint(CardRunMachine::kMainOut);
    ASSERT_NE(id, BRK_INVALID);
    execution::RunReply stopped = m.Run("command");
    ASSERT_TRUE(stopped.Ok()) << stopped.message;
    EXPECT_EQ(Reason(stopped), "breakpoint") << stopped.message;
    EXPECT_EQ(stopped.body["stop"]["cpu"].s, "main");
    EXPECT_FALSE(stopped.body["hit"].b);
    EXPECT_EQ(stopped.body["main_pc"].i, CardRunMachine::kMainOut);
    main->RemoveBreakpointByID(id);

    execution::RunReply reply = m.Run("command");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Reason(reply), "step") << reply.message;
    EXPECT_TRUE(reply.body["hit"].b);
    EXPECT_EQ(reply.body["command"].i, 0x23);
    EXPECT_EQ(reply.body["stop"]["command"].i, 0x23);
    EXPECT_EQ(reply.body["stop"]["cpu"].s, "gs");
    EXPECT_EQ(reply.body["main_pc"].i, CardRunMachine::kMainLoop) << "paused after the OUT";
    EXPECT_NE(reply.message.find("#23"), std::string::npos) << reply.message;
    EXPECT_EQ(m.card->getStatusRaw() & 0x01, 0x01) << "the card has not taken it";
    EXPECT_EQ(m.card->getCommandFromHost(), 0x23);
    EXPECT_FALSE(m.context->pDebugManager->GetGSTarget()->StepArmed());
    EXPECT_TRUE(m.emulator->IsPaused());

    // No command comes (the main CPU loops): the deadline pauses it with the reason timeout - not run here (5 s);
    // a refusal while one is armed instead
    ASSERT_EQ(m.context->pDebugManager->GetGSTarget()->ArmUntil(GSCardTarget::StepKind::Command), "");
    EXPECT_EQ(m.Run("command").status, execution::RunReply::Status::Refused) << "one run at a time";
    m.context->pDebugManager->GetGSTarget()->CancelStep();
}

// Run the main CPU to a T-state of the frame from the card's view: reached (the first boundary at or after it), the
// next frame's when already past, a main breakpoint on the way ends it; out of the frame refused
TEST(GSCardRunTo_Test, TheMainCpuRunsToAFrameT)
{
    CardRunMachine m;
    ASSERT_TRUE(m.Start());
    LoadRoutine(*m.card);
    m.MainProgram();
    Z80* z80 = m.context->pCore->GetZ80();
    const uint32_t frame = z80->_frameLimit;
    ASSERT_GT(frame, 0u);

    const uint64_t ahead = (z80->t + 1000) % frame;
    const uint64_t frameBefore = m.context->emulatorState.frame_counter;
    execution::RunReply reply = m.Run("main_t", ahead);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Reason(reply), "step") << reply.message;
    EXPECT_EQ(reply.body["stop"]["cpu"].s, "main");
    EXPECT_TRUE(reply.body["hit"].b);
    EXPECT_EQ(reply.body["until"].s, "main_t");
    EXPECT_EQ(reply.body["target_t"].i, static_cast<int64_t>(ahead));
    EXPECT_GE(reply.body["main_t"].i, static_cast<int64_t>(ahead));
    EXPECT_LT(reply.body["main_t"].i, static_cast<int64_t>(ahead) + 30) << "one instruction past it at most";
    EXPECT_NE(reply.body.find("pc"), nullptr) << "the card's registers";
    EXPECT_TRUE(m.emulator->IsPaused());

    const uint64_t behind = static_cast<uint64_t>(reply.body["main_t"].i) / 2;
    execution::RunReply next = m.Run("main_t", behind);
    ASSERT_TRUE(next.Ok()) << next.message;
    EXPECT_TRUE(next.body["hit"].b);
    EXPECT_GE(next.body["main_t"].i, static_cast<int64_t>(behind));
    EXPECT_GT(next.body["frame"].i, reply.body["frame"].i) << "the next frame's";
    EXPECT_GE(static_cast<uint64_t>(reply.body["frame"].i), frameBefore);

    // The main CPU loops at JR $ after its OUT: a breakpoint there ends the run first
    const uint16_t id = m.context->pDebugManager->GetBreakpointsManager()->AddExecutionBreakpoint(CardRunMachine::kMainLoop);
    ASSERT_NE(id, BRK_INVALID);
    execution::RunReply stopped = m.Run("main_t", (static_cast<uint64_t>(next.body["main_t"].i) + frame / 2) % frame);
    ASSERT_TRUE(stopped.Ok()) << stopped.message;
    EXPECT_EQ(Reason(stopped), "breakpoint") << stopped.message;
    EXPECT_FALSE(stopped.body["hit"].b);
    EXPECT_EQ(stopped.body["main_pc"].i, CardRunMachine::kMainLoop);
    m.context->pDebugManager->GetBreakpointsManager()->RemoveBreakpointByID(id);

    EXPECT_EQ(m.Run("main_t", frame).status, execution::RunReply::Status::BadRequest);
}
