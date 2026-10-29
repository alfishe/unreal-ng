// The General Sound card CPU as a debug target (gs-debugger design §3-§4): its own breakpoints, the card hooks
// swapped in only while a breakpoint asks for them, execute stops before the instruction, watchpoints and card
// events reported at the next instruction boundary with the PC of the instruction that did the access.
//
// The card runs the real gs105a firmware by itself (no machine): a stop is recorded by the target (LastStop), there
// is no emulator to pause, so the card runs on past it. After POST the firmware waits for the host to take its reply
// byte, then polls the mailbox (boot()).

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/slots/cards/multisound/multisoundstagedmachine.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

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
    spec.condition = "IN == 4 && M(026E) == 00";  // the ROM holds DB there
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
        EXPECT_NE(error.find("the machine's, not this CPU's"), std::string::npos) << error;
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
