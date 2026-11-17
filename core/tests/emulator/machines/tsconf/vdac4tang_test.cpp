// TS-Conf VDAC4 board: the Tang FPGA's personalities as the Evo sees them
// (docs/inprogress/2026-11-15-tsconf-vdac4/design.md §5.3, research-tang-video-ft-core.md §3).
//
// The Tang sits where the VDAC2 card's FT812 sits. In slot 0 (TS-Conf core) it
// answers 0 and watches the Evo's FT812 traffic; once armed, an ACTIVE or a
// memory access loads slot 1 (the FT812 core, ACTIVE at 64 MHz); with M_SEL = 0
// and 500 ms of silence it falls back and the FT812 core's memory is gone.

#ifdef ENABLE_VDAC2

#include "vdac2cardfixture.h"

#include <thread>
#include <vector>

#include "emulator/platforms/tsconf/vdac4/vdac4ftworker.h"
#include "emulator/platforms/tsconf/vdac4/vdac4tang.h"

using namespace Vdac2Test;

namespace
{
constexpr uint64_t kTactsPerMs = Vdac4Tang::kTactsPerMs;
constexpr uint32_t kRegFrequency = 0x30200C;
}  // namespace

class Vdac4Tang_Test : public Vdac2CardFixture
{
protected:
    void SetUp() override
    {
        Vdac2CardFixture::SetUp();
        _context->config.ts_vdac4 = true;
        _context->config.vdac4_esp_boot_ms = 400;
        _context->config.vdac4_load_delay_ms = 174;
        _decoder->reset();
    }

    Vdac4Tang* Tang() { return _decoder->GetVdac4Tang(); }

    /// Advance and bring the engine's raster up to the CPU: the Tang's clock (LINK accesses come from the ESP, not
    /// from a port access that would catch the engine up)
    void TickMs(uint64_t ms)
    {
        Tick(ms * kTactsPerMs);
        _decoder->CatchUpEngine();
    }

    /// Past the ESP's boot: the Tang is armed
    void PastEspBoot() { TickMs(_context->config.vdac4_esp_boot_ms + 1); }

    /// Poll REG_ID every millisecond until it reads 0x7C; the milliseconds it took, or -1
    int MsUntilRegId(int limitMs)
    {
        for (int ms = 0; ms <= limitMs; ms++)
        {
            if (Read(kRegId, 1)[0] == kRegIdValue)
                return ms;
            TickMs(1);
        }
        return -1;
    }
};

/// TS_VDAC4 fits the Tang in the VDAC2 build's FT812 place; STATUS still says 7
TEST_F(Vdac4Tang_Test, FittedOnTheVdac2Build)
{
    ASSERT_NE(Tang(), nullptr);
    EXPECT_EQ(Card(), Tang());
    EXPECT_EQ(_decoder->ReadRegister(kStatusRegister) & kVdacVersionMask, 7);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);

    _context->config.ts_vdac4 = false;
    _decoder->reset();
    EXPECT_EQ(Tang(), nullptr);
    ASSERT_NE(Card(), nullptr) << "the plain VDAC2 card again";
}

/// Before the ESP has booted the Tang is not armed: the FT812 never appears
TEST_F(Vdac4Tang_Test, DisarmedSlotZeroIgnoresActive)
{
    EXPECT_FALSE(Tang()->IsArmed());
    HostCommand(kHostActive);
    TickMs(300);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
    EXPECT_EQ(Read(kRegId, 1)[0], 0) << "slot 0 has no FT812: MISO reads 0";
}

/// Armed, an ACTIVE loads the FT812 core: REG_ID reads 0 for the load time,
/// then 0x7C with no further boot
TEST_F(Vdac4Tang_Test, ActiveLoadsTheFtCoreAlreadyRunning)
{
    PastEspBoot();
    ASSERT_TRUE(Tang()->IsArmed());
    HostCommand(kHostActive);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Loading);

    TickMs(170);
    EXPECT_EQ(Read(kRegId, 1)[0], 0) << "still loading";
    const int ms = MsUntilRegId(100);
    ASSERT_GE(ms, 0);
    EXPECT_LE(ms, 30) << "174 ms load, then the core is ACTIVE";
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ft);
    // REG_FREQUENCY is a plain register at its reset value (the core's clock is 64 MHz regardless)
    EXPECT_EQ(Read32(kRegFrequency), 60'000'000u);
}

/// Plain host commands (CLKEXT, CLKSEL, RST_PULSE) do not trigger the load (core 1.16+)
TEST_F(Vdac4Tang_Test, PlainHostCommandsDoNotTrigger)
{
    PastEspBoot();
    HostCommand(kHostClkExt);
    HostCommand(kHostClkSel, kMul48MHz | kClkSelRangeBits);
    HostCommand(kHostRstPulse);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
}

/// A memory access triggers too; what was written before the switch is lost
TEST_F(Vdac4Tang_Test, MemoryAccessTriggersAndEarlyWritesAreLost)
{
    PastEspBoot();
    Write(kRamG + 0x100, {0x11, 0x22, 0x33, 0x44});
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Loading);
    ASSERT_GE(MsUntilRegId(400), 0);
    EXPECT_EQ(Read(kRamG + 0x100, 4), std::vector<uint8_t>(4, 0));
    Write(kRamG + 0x100, {0x11, 0x22, 0x33, 0x44});
    EXPECT_EQ(Read(kRamG + 0x100, 4), (std::vector<uint8_t>{0x11, 0x22, 0x33, 0x44}));
}

/// M_SEL = 0 and 500 ms without FT traffic: back to slot 0, the FT core's memory gone
TEST_F(Vdac4Tang_Test, IdleFallbackLosesTheFtCore)
{
    PastEspBoot();
    HostCommand(kHostActive);
    ASSERT_GE(MsUntilRegId(400), 0);
    Write(kRamG, {0x5A});

    TickMs(Vdac4Tang::kSwitchIdleMs + 25);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);

    HostCommand(kHostActive);
    ASSERT_GE(MsUntilRegId(400), 0);
    EXPECT_EQ(Read(kRamG, 1)[0], 0) << "a fresh FT812 core";
}

/// M_SEL = 1 holds the FT core however long the Evo is silent
TEST_F(Vdac4Tang_Test, MselHoldsTheFtCore)
{
    PastEspBoot();
    HostCommand(kHostActive);
    ASSERT_GE(MsUntilRegId(400), 0);
    _decoder->WriteRegister(TsConfReg::VConfig, kMsel);
    TickMs(2000);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ft);
}

/// FT_CFG bit 2 (ESP host): the Evo reads 0 and its writes are dropped
TEST_F(Vdac4Tang_Test, EspHostCutsOffTheEvo)
{
    PastEspBoot();
    HostCommand(kHostActive);
    ASSERT_GE(MsUntilRegId(400), 0);
    Tang()->LinkWrite(Vdac4Tang::LinkFtStat, Vdac4Tang::kFtCfgEspHost);
    EXPECT_EQ(Read(kRegId, 1)[0], 0);
    TickMs(Vdac4Tang::kSwitchIdleMs * 2);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ft) << "the idle timer is frozen";
    Tang()->LinkWrite(Vdac4Tang::LinkFtStat, 0);
    EXPECT_EQ(Read(kRegId, 1)[0], kRegIdValue);
}

/// The ESP's LINK: ID, version with the personality, and the FT window into RAM_G
TEST_F(Vdac4Tang_Test, LinkRegistersAndFtWindow)
{
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkId), 0x56444334u);
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkVersion) >> 24, 1u) << "slot 0";

    Tang()->LinkWrite(Vdac4Tang::LinkBoot, 0x41524D31);  // ARM1
    Tang()->LinkWrite(Vdac4Tang::LinkBoot, 0x424F4F54);  // BOOT
    TickMs(_context->config.vdac4_load_delay_ms + 1);
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkVersion) >> 24, 2u) << "slot 1";

    Tang()->LinkWrite(Vdac4Tang::LinkFtAddr, kRamG + 0x40);
    Tang()->LinkWrite(Vdac4Tang::LinkFtData, 0xA1B2C3D4);
    Tang()->LinkWrite(Vdac4Tang::LinkFtData, 0x01020304);
    EXPECT_EQ(Read32(kRamG + 0x40), 0xA1B2C3D4u);
    EXPECT_EQ(Read32(kRamG + 0x44), 0x01020304u);
    Tang()->LinkWrite(Vdac4Tang::LinkFtAddr, kRamG + 0x44);
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkFtData), 0x01020304u);
}

/// The FT worker (the Tang's work for an ESP job): the ops of a producer thread applied in order, on its own thread
/// or at once, a read seeing every write before it; more ops than its ring holds wait for room, nothing is lost
TEST_F(Vdac4Tang_Test, FtWorkerAppliesInOrder)
{
    Tang()->LinkWrite(Vdac4Tang::LinkBoot, 0x41524D31);  // ARM1
    Tang()->LinkWrite(Vdac4Tang::LinkBoot, 0x424F4F54);  // BOOT
    TickMs(_context->config.vdac4_load_delay_ms + 1);
    ASSERT_EQ(Tang()->LinkRead(Vdac4Tang::LinkVersion) >> 24, 2u) << "slot 1";

    constexpr uint32_t kWords = 70'000;  // over the worker's 65536-op ring
    for (const bool threaded : {false, true})
    {
        const uint32_t salt = threaded ? 0x9E3779B9u : 0x7F4A7C15u;
        auto word = [salt](uint32_t i) { return i * 2654435761u ^ salt; };
        Vdac4FtWorker ft(*Tang());
        ft.SetThreaded(threaded);
        const uint64_t stamp = Tang()->CurrentTact();
        uint32_t readBack = 0;
        std::thread producer([&]() {
            ft.Write(stamp, Vdac4Tang::LinkFtAddr, kRamG);
            for (uint32_t i = 0; i < kWords; ++i)
                ft.Write(stamp, Vdac4Tang::LinkFtData, word(i));
            ft.Write(stamp, Vdac4Tang::LinkFtAddr, kRamG + 4 * (kWords - 1));
            readBack = ft.Read(stamp, Vdac4Tang::LinkFtData);
            ft.EndJob(stamp, 0);
        });
        producer.join();
        EXPECT_FALSE(ft.JobHosted(0)) << "the Evo is the FT812's host";
        ft.WaitIdle();
        EXPECT_EQ(readBack, word(kWords - 1)) << "threaded " << threaded;
        for (uint32_t i : {0u, 1u, 4095u, 65535u, 65536u, kWords - 1})
            EXPECT_EQ(Read32(kRamG + 4 * i), word(i)) << "word " << i << " threaded " << threaded;
        EXPECT_EQ(ft.GetStats().reads, 1u);
        if (threaded)
            EXPECT_EQ(ft.GetStats().ops, kWords + 3u);  // the writes and the job's end
    }
}

#endif // ENABLE_VDAC2
