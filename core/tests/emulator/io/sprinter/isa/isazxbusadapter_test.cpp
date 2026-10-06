// The ZX-bus adapter in a Sprinter ISA slot with the General Sound / NeoGS behind it (isazxbusadapter.h;
// docs/inprogress/2026-10-02-sprinter-isa/tdd.md §6-7, phase I2): the default population, ISA I/O cycles reaching the
// card's mailbox (the firmware-independent part: the host flip-flops), the GS address decode, AEN, peeks without side
// effects, ISA RESET DRV, the populations without a GS, the report every surface prints, the NeoGS ZX-DMA that cannot
// install without host memory cycles, and the TTD port journals that record with the NeoGS fitted.
//
// Machine-level but short: no firmware run is needed (the mailbox flip-flops answer at once); each test creates one
// Sprinter instance.
//
// The shipped Sprinter config fits the NeoGS behind the adapter ([SLOTS] isa.1 = neogs, owner decision 2026-10-06).
// The tests about the card also select it explicitly at creation, so they do not depend on the shipped file.

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/sprinter/isa/cards/isazxbusadapter.h"
#include "emulator/io/sprinter/isa/isaaccess.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

using sprinterisa::CardKind;

class IsaZxBusAdapter_Test : public ::testing::Test
{
protected:
    // The test runner leaves the GS out of every machine; these tests are about it
    SoundCardScope _gsScope{TestSound::GeneralSound};
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }
    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    /// A Sprinter with the NeoGS behind the adapter (the field the slot set is translated from at creation), then
    /// @p configOverride
    void Create(std::function<void(CONFIG&)> configOverride = nullptr)
    {
        CreateShipped([extra = std::move(configOverride)](CONFIG& config) {
            config.sound.gsTypeKind = GSTypeKind::NGS;
            if (extra)
                extra(config);
        });
    }

    /// A Sprinter as the shipped config makes it (the NeoGS behind the adapter), then @p configOverride
    void CreateShipped(std::function<void(CONFIG&)> configOverride = nullptr)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-zxbus", "SPRINTER", 4096, LoggerLevel::LogError,
                                                            nullptr, std::move(configOverride));
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    GeneralSoundCard* Gs() { return _context->pSoundManager->getGeneralSound(); }

    uint8_t Isa(const std::string& action, int slot, uint32_t address, int value = -1)
    {
        StateNode result;
        std::string error;
        EXPECT_TRUE(IsaAccess::Execute(_context, action, slot, address, value, "test", result, error)) << error;
        const StateNode* v = result.find("value");
        return v ? static_cast<uint8_t>(std::strtoul(v->s.c_str() + 1, nullptr, 16)) : 0;
    }
};

// Owner decision Q2: slot 1 = the ZX-bus adapter, slot 2 = the NE2000. Owner decision 2026-10-06: the shipped config
// puts the NeoGS behind the adapter. The machine has a ZX-bus only through the adapter; its bus carries no memory
// cycles
TEST_F(IsaZxBusAdapter_Test, DefaultPopulation_NeoGsBehindTheAdapterInSlot1)
{
    CreateShipped();
    ASSERT_NE(_decoder->GetIsaBus().Card(0), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(0)->Kind(), "zxbus");
    EXPECT_TRUE(_decoder->ZxBusPresent()) << "the adapter is there";
    EXPECT_FALSE(_decoder->ZxBusMemoryCycles());
    EXPECT_EQ(_decoder->ZxBusSlot(), 0);
    ASSERT_NE(Gs(), nullptr) << "the shipped config fits the NeoGS";
    EXPECT_EQ(Gs()->implementation(), GSCardImplementation::NGS);
    ASSERT_NE(_decoder->GetIsaBus().Card(1), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(1)->Kind(), "ne2000");
    EXPECT_EQ(_context->config.sound.gsreset, 0) << "Q9: a machine reset does not pulse ISA RESET DRV";
}

// The NeoGS added behind the adapter (a user's [SLOTS] isa.1 = neogs): SoundManager fits it there
TEST_F(IsaZxBusAdapter_Test, AddedNeoGsSitsBehindTheAdapter)
{
    Create();
    EXPECT_TRUE(_decoder->ZxBusPresent());
    EXPECT_EQ(_decoder->ZxBusSlot(), 0);
    ASSERT_NE(Gs(), nullptr) << "SoundManager fits the GS behind the adapter";
    EXPECT_EQ(Gs()->implementation(), GSCardImplementation::NGS);
}

// ProPlay's path: ISA I/O #0B3 / #0BB of slot 1 are the GS data and command / status ports. The host flip-flops answer
// without the firmware: a data write raises status bit 7, a data read clears it; a command write raises bit 0
TEST_F(IsaZxBusAdapter_Test, IoCyclesReachTheGsMailbox)
{
    Create();
    GeneralSoundCard* gs = Gs();
    ASSERT_NE(gs, nullptr);
    Isa("io_write", 1, 0x0B3, 0x5A);
    EXPECT_EQ(gs->getDataFromHost(), 0x5A);
    EXPECT_EQ(gs->getStatusRaw() & 0x80, 0x80) << "the data flip-flop";
    EXPECT_EQ(Isa("io_read", 1, 0x0BB) & 0xFE, 0xFE) << "status: bit 7 data, bits 6-1 read as 1";
    const uint8_t status = Isa("io_peek", 1, 0x0BB);
    EXPECT_EQ(status, static_cast<uint8_t>(gs->getStatusRaw() | 0x7E)) << "a peek shows the status";

    // The GS decodes A7-A0: #1BB, #C0BB-style mirrors and any A19-A14 reach the same port
    Isa("io_write", 1, 0x1C0BB, 0xF3);
    EXPECT_EQ(gs->getCommandFromHost(), 0xF3);
    EXPECT_EQ(Isa("io_read", 1, 0x0BC), 0xFF) << "no GS port: nothing drives the bus";
    EXPECT_EQ(Isa("io_read", 1, 0x033), 0xFF) << "#33 is write-only";
    EXPECT_EQ(Isa("mem_read", 1, 0x000BB), 0xFF) << "no memory cycles through the adapter";
    EXPECT_EQ(Isa("io_read", 2, 0x0BB), 0xFF) << "slot 2 holds the NE2000, not the GS";

    // AEN = 1: the adapter does not turn the cycle into a ZX-bus access
    Isa("latch", 0, 0, 0x40);
    EXPECT_EQ(Isa("io_read", 1, 0x0BB), 0xFF);
    Isa("io_write", 1, 0x0B3, 0x11);
    EXPECT_EQ(gs->getDataFromHost(), 0x5A);
    Isa("latch", 0, 0, 0x00);
}

// T-ISA-5: a debugger read of the data port shows the byte without the read's side effect (the data flag stays)
TEST_F(IsaZxBusAdapter_Test, PeekHasNoSideEffect)
{
    Create();
    GeneralSoundCard* gs = Gs();
    ASSERT_NE(gs, nullptr);
    Isa("io_write", 1, 0x0B3, 0x42);   // the host's data flip-flop up
    ASSERT_EQ(gs->getStatusRaw() & 0x80, 0x80);
    const uint8_t before = gs->getStatusRaw();
    Isa("io_peek", 1, 0x0B3);
    Isa("io_peek", 1, 0x0BB);
    EXPECT_EQ(gs->getStatusRaw(), before);
    Isa("io_read", 1, 0x0B3);          // a real read clears it
    EXPECT_EQ(gs->getStatusRaw() & 0x80, 0);
}

// Q3: ISA RESET DRV reaches the GS through the adapter (the #33 bit-7 card reset at each edge); while it is held the
// bus passes no cycle to the card. A machine reset does not touch the latch, so the card is not reset by it
TEST_F(IsaZxBusAdapter_Test, IsaResetDrvResetsTheGs)
{
    Create();
    GeneralSoundCard* gs = Gs();
    ASSERT_NE(gs, nullptr);
    auto* adapter = dynamic_cast<sprinterisa::IsaZxBusAdapter*>(_decoder->GetIsaBus().Card(0));
    ASSERT_NE(adapter, nullptr);
    gs->startPortTrace();
    Isa("latch", 0, 0, 0xC0);   // RESET DRV + AEN, as ESSMIXER and the Wi-Fi kit write it
    EXPECT_TRUE(adapter->ResetHeld());
    EXPECT_EQ(Isa("io_read", 1, 0x0BB), 0xFF) << "held in reset: no cycle reaches the card";
    Isa("latch", 0, 0, 0x00);
    EXPECT_FALSE(adapter->ResetHeld());
    int resets = 0;
    for (const GSTraceEvent& e : gs->getPortTraceEvents())
        resets += e.side == GSTraceSide::Host && e.port == GeneralSoundCard::PORT_CONTROL && e.isOut() && e.value == 0x80;
    EXPECT_EQ(resets, 2) << "the card reset at the edge to held and again at the release";

    const StateNode isa = DeviceState::Isa(_context);
    const StateNode* zx = isa.find("slots")->items[0].find("zx_bus");
    ASSERT_NE(zx, nullptr);
    EXPECT_EQ(zx->find("reset_pulses")->i, 1);
}

// The one line every surface shows, and the slot's ZX-bus object: what is plugged where and what it uses
TEST_F(IsaZxBusAdapter_Test, ReportShowsTheAdapterAndTheCardBehindIt)
{
    Create();
    const StateNode isa = DeviceState::Isa(_context);
    const std::string summary = isa.find("summary")->s;
    EXPECT_NE(summary.find("slot 1: zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV"),
              std::string::npos)
        << summary;
    const StateNode& slot1 = isa.find("slots")->items[0];
    EXPECT_EQ(slot1.find("card")->s, "zxbus");
    EXPECT_EQ(slot1.find("resources")->find("io")->s, "#033-#0BB");
    EXPECT_NE(slot1.find("z80_access")->find("io")->s.find("page #D4"), std::string::npos);
    EXPECT_NE(slot1.find("summary_line")->s.find("NeoGS"), std::string::npos);
    const StateNode* zx = slot1.find("zx_bus");
    ASSERT_NE(zx, nullptr);
    const StateNode* cards = zx->find("cards");
    ASSERT_NE(cards, nullptr);
    ASSERT_EQ(cards->items.size(), 1u);
    const StateNode& gs = cards->items[0];
    EXPECT_EQ(gs.find("card")->s, "gs");
    EXPECT_EQ(gs.find("personality")->s, "ngs");
    EXPECT_NE(gs.find("cpu_addresses")->s.find("#C0BB"), std::string::npos);
    EXPECT_NE(gs.find("zx_dma")->s.find("unavailable"), std::string::npos);
    EXPECT_NE(gs.find("machine_reset")->s.find("GSReset=0"), std::string::npos) << gs.find("machine_reset")->s;

    // The journal names the GS ports
    _decoder->GetIsaBus().ClearJournal();
    Isa("io_write", 1, 0x0BB, 0xF3);
    Isa("io_read", 1, 0x0BB);
    const StateNode journal = DeviceState::IsaJournal(_context, 0);
    const StateNode* entries = journal.find("entries");
    ASSERT_NE(entries, nullptr);
    ASSERT_EQ(entries->items.size(), 2u);
    EXPECT_EQ(entries->items[0].find("what")->s, "GS command (#BB)");
    EXPECT_EQ(entries->items[1].find("what")->s, "GS status (#BB)");
}

// Slot1=NONE: no adapter, no ZX-bus, no GS built (the frame cost of a GS the software cannot reach is gone)
TEST_F(IsaZxBusAdapter_Test, NoAdapter_NoGs)
{
    Create([](CONFIG& config) { config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(CardKind::None); });
    EXPECT_EQ(_decoder->GetIsaBus().Card(0), nullptr);
    EXPECT_FALSE(_decoder->ZxBusPresent());
    EXPECT_EQ(Gs(), nullptr);
    EXPECT_EQ(Isa("io_read", 1, 0x0BB), 0xFF);
}

// One GS per machine: a second adapter (slot 2) has an empty ZX-bus, the report says why; GSType=NONE leaves the
// adapter with nothing behind it
TEST_F(IsaZxBusAdapter_Test, SecondAdapterAndNoGsType)
{
    Create([](CONFIG& config) { config.sprinter.isa.slot[1].kind = static_cast<uint8_t>(CardKind::ZxBus); });
    ASSERT_NE(_decoder->GetIsaBus().Card(1), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(1)->Kind(), "zxbus");
    EXPECT_EQ(_decoder->ZxBusSlot(), 0);
    Isa("io_write", 1, 0x0B3, 0x77);
    EXPECT_EQ(Isa("io_read", 2, 0x0BB), 0xFF) << "the second adapter's ZX-bus is empty";
    EXPECT_EQ(Isa("io_read", 1, 0x0BB) & 0x80, 0x80);
    const StateNode isa = DeviceState::Isa(_context);
    const StateNode* empty = isa.find("slots")->items[1].find("zx_bus")->find("empty");
    ASSERT_NE(empty, nullptr);
    EXPECT_NE(empty->s.find("one General Sound per machine"), std::string::npos) << empty->s;
    TearDown();

    Create([](CONFIG& config) { config.sound.gsTypeKind = GSTypeKind::NONE; });
    EXPECT_TRUE(_decoder->ZxBusPresent()) << "the adapter is there";
    EXPECT_EQ(Gs(), nullptr);
    EXPECT_EQ(Isa("io_read", 1, 0x0BB), 0xFF) << "nothing on its ZX-bus";
    EXPECT_NE(DeviceState::Isa(_context).find("summary")->s.find("ZX-bus empty"), std::string::npos);
}

// The NeoGS ZX-DMA needs the host's memory cycles; the adapter passes none, so the module's overlay never installs
// (ZX-DMA watch "always" would install it on a Pentagon at once), and the TTD port journals record with the NeoGS
// fitted: every host access to the card is an ISA cycle of the machine's own state
TEST_F(IsaZxBusAdapter_Test, NeoGsZxDmaCannotInstall_PortJournalRecords)
{
    Create([](CONFIG& config) { config.ngs.zxDmaWatch = NeoGSConfig::ZxDmaWatch::Always; });
    GeneralSoundCard* gs = Gs();
    ASSERT_NE(gs, nullptr);
    _emulator->RunNFrames(2, true);
    NeoGSStateInfo info;
    ASSERT_TRUE(gs->neogsState(info));
    EXPECT_FALSE(info.zxOverlayInstalled) << "no host memory bus on the adapter";
    EXPECT_FALSE(info.zxHostMemoryBus);

    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(2, true);
    ttd->StopRecording();
    const ttd::TTDSessionInfo session = ttd->GetSessionInfo();
    EXPECT_TRUE(session.portJournalActive) << session.portJournalOffReason;
}
