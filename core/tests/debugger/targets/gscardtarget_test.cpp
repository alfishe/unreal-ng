// The General Sound card CPU as a debug target (gs-debugger design §3-§4): its own breakpoints, the card hooks
// swapped in only while a breakpoint asks for them, execute stops before the instruction, watchpoints and card
// events reported at the next instruction boundary with the PC of the instruction that did the access.
//
// The card runs the real gs105a firmware by itself (no machine): a stop is recorded by the target (LastStop), there
// is no emulator to pause, so the card runs on past it. After POST the firmware waits for the host to take its reply
// byte, then polls the mailbox (boot()).

#include <gtest/gtest.h>

#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
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
    BreakpointSpec spec = Exec(0);
    spec.condition = "A == 1";
    EXPECT_EQ(card.bp().AddBreakpoint(spec, error), BRK_INVALID);
    EXPECT_NE(error.find("not supported"), std::string::npos) << error;

    BreakpointSpec magic;
    magic.type = BRK_MAGIC;
    magic.address = BRK_MAGIC_ED_FF;
    EXPECT_EQ(card.bp().AddBreakpoint(magic, error), BRK_INVALID);

    EXPECT_EQ(card.bp().AddBreakpoint(Event(GSCardEvent::HostCommandWrite), error), BRK_INVALID)
        << "the host side is the main CPU's";
    EXPECT_FALSE(card.target->HooksAttached());
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
