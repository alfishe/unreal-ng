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
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/slots/cards/multisound/multisoundstagedmachine.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
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
// instruction after the CALL with the entry SP, out after the taken RET (RET Z not taken does not end it)
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
                          Case{GSCardTarget::StepKind::Out, 1, kRoutine, kAfterCall}})
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
        ASSERT_EQ(card.target->ArmStep(c.kind, c.count), "");
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
    auto step = [&](const char* kind, unsigned count, const char* reason, uint16_t pc) {
        const execution::RunReply reply = control.CardStep(requester, DebugCpuId::GS, kind, count);
        EXPECT_TRUE(reply.Ok()) << kind << ": " << reply.message;
        const StateNode* stop = reply.body.find("stop");
        EXPECT_NE(stop, nullptr) << kind;
        if (!stop)
            return;
        EXPECT_EQ(stop->find("reason")->s, reason) << kind << ": " << reply.message;
        EXPECT_EQ(stop->find("cpu")->s, "gs") << kind;
        EXPECT_EQ(reply.body.find("pc")->i, pc) << kind;
        EXPECT_NE(reply.body.find("main_pc"), nullptr);
        EXPECT_EQ(reply.body.find("state")->s, "paused") << kind;
        EXPECT_TRUE(emulator->IsPaused());
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

    const execution::RunReply refused = control.CardStep(requester, DebugCpuId::MultiSoundGS, "in", 1);
    EXPECT_FALSE(refused.Ok()) << "no MultiSound in this machine";
    EXPECT_EQ(control.CardStep(requester, DebugCpuId::GS, "sideways", 1).status,
              execution::RunReply::Status::BadRequest);
    emulator->Stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
