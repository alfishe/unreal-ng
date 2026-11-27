// TS-Conf VDAC4 board: the Tang FPGA's personalities as the Evo sees them
// (docs/inprogress/2026-11-15-tsconf-vdac4/design.md §5.3, research-tang-video-ft-core.md §3).
//
// The Tang sits where the VDAC2 card's FT812 sits. In slot 0 (TS-Conf core) it
// answers 0 and watches the Evo's FT812 traffic; once armed, an ACTIVE or a
// memory access loads slot 1 (the FT812 core, ACTIVE at 64 MHz); with M_SEL = 0
// and 500 ms of silence it falls back and the FT812 core's memory is gone.

#ifdef ENABLE_VDAC2

#include "vdac2cardfixture.h"

#include <cstring>
#include <initializer_list>
#include <thread>
#include <vector>

#include "emulator/platforms/tsconf/vdac4/vdac4bomzh.h"
#include "emulator/platforms/tsconf/vdac4/vdac4espstate.h"
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

/// core_load of the Quake II core (quake2.md): the Tang answers as bomzh-3D on the LINK whatever M_SEL says, the
/// Evo reads nothing from it; the Evo's RST_PULSE (its BIOS resetting the video card) brings slot 0 back
TEST_F(Vdac4Tang_Test, Quake2CoreLoadsAndTheEvoResetBringsSlotZero)
{
    PastEspBoot();
    uint32_t version = 0;
    EXPECT_EQ(Tang()->LoadCore(0x56443446, version), -1) << "no emulated core with that mark";
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    EXPECT_EQ(version, 0x51010003u);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Q2);
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkVersion), 0x51010003u);
    ASSERT_NE(Tang()->Q2Core(), nullptr);
    // A frame through the queue
    Tang()->LinkWrite(0x40000, 0x0700'0000u);
    Tang()->LinkWrite(0x40000, 0);
    EXPECT_EQ(Tang()->LinkRead(0x10), 1u);
    _decoder->WriteRegister(TsConfReg::VConfig, kMsel);
    TickMs(2000);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Q2) << "M_SEL = 1 holds it";
    EXPECT_EQ(Read(kRegId, 1)[0], 0) << "the Evo's SPI gets nothing back";
    HostCommand(kHostRstPulse);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
    EXPECT_EQ(Tang()->Q2Core(), nullptr);
    EXPECT_EQ(Tang()->LinkRead(Vdac4Tang::LinkVersion) >> 24, 1u);
}

/// BOOT over the LINK and CoreBack go back to slot 0 too
TEST_F(Vdac4Tang_Test, Quake2CoreBackAndBoot)
{
    PastEspBoot();
    uint32_t version = 0;
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    Tang()->LinkWrite(Vdac4Tang::LinkBoot, 0x424F4F54);
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    EXPECT_TRUE(Tang()->CoreBack());
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
}

/// Time travel over core_load / core_back (quake2.md §8): a state taken while the Quake II core runs brings it back
/// as it was over a Tang in slot 0 or running the core with another history; a slot-0 state leaves the core
TEST_F(Vdac4Tang_Test, Quake2StateRestoresAcrossCoreLoadAndBack)
{
    PastEspBoot();
    uint32_t version = 0;
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    auto queue = [&](std::initializer_list<uint32_t> words) {
        for (uint32_t w : words)
            Tang()->LinkWrite(0x40000, w);
    };
    queue({0x0A00'0000u, 0x10000, 0x20000, 0x30000, (8u << 16) | 16u, 0x40000, 0, 0x50000, 0x60000});
    queue({0x0600'0000u, 0, (8u << 16) | 16u, 7, 0x0700'0000u, 0});  // a frame filled with color 7, shown
    queue({0x0900'0003u, 0x200000, 0x11223344u});                      // inside a WRITE
    Tang()->PresentFrame();
    EXPECT_EQ(Tang()->Q2LatchedFrames(), 1u);
    auto save = [&] {
        std::vector<uint8_t> bytes;
        Vdac4EspStateWriter w(bytes);
        Tang()->SaveQ2(w);
        return bytes;
    };
    auto load = [&](const std::vector<uint8_t>& bytes) {
        Vdac4EspStateReader r(bytes.data(), bytes.size());
        return Tang()->LoadQ2(r) && r.AtEnd();
    };
    const Vdac4Tang::State q2State = Tang()->GetState();
    const std::vector<uint8_t> q2Bytes = save();
    const std::vector<uint8_t> sdram(Tang()->Q2Model()->Memory(),
                                     Tang()->Q2Model()->Memory() + Vdac4BomzhCore::kMemoryBytes);

    // core_back, then a new core_load with another history: the model is the same memory, reset
    const uint8_t* memory = Tang()->Q2Model()->Memory();
    ASSERT_TRUE(Tang()->CoreBack());
    const Vdac4Tang::State tsState = Tang()->GetState();
    const std::vector<uint8_t> tsBytes = save();
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    EXPECT_EQ(Tang()->Q2Model()->Memory(), memory) << "the SDRAM stays where it is (a TTD region)";
    EXPECT_EQ(Tang()->Q2Core()->Frames(), 0u) << "core_load resets the core";
    queue({0x0700'0000u, 0, 0x0700'0000u, 0});

    // The Quake II state back over the other history (the engine restores the SDRAM first)
    std::memcpy(Tang()->Q2Model()->Memory(), sdram.data(), sdram.size());
    Tang()->SetState(q2State);
    ASSERT_TRUE(load(q2Bytes));
    EXPECT_EQ(save(), q2Bytes);
    EXPECT_EQ(Tang()->Q2LatchedFrames(), 1u);

    // The slot-0 state leaves the core; the Quake II state over slot 0 enters it again, the WRITE goes on
    Tang()->SetState(tsState);
    ASSERT_TRUE(load(tsBytes));
    EXPECT_EQ(Tang()->GetPersonality(), Vdac4Tang::Personality::Ts);
    EXPECT_EQ(Tang()->Q2Core(), nullptr);
    Tang()->SetState(q2State);
    ASSERT_TRUE(load(q2Bytes));
    ASSERT_NE(Tang()->Q2Core(), nullptr);
    EXPECT_EQ(save(), q2Bytes);
    queue({0x5566'7788u, 0x99AA'BBCCu});
    const uint8_t* m = Tang()->Q2Model()->Memory() + 0x200000;
    EXPECT_EQ(m[0], 0x44);
    EXPECT_EQ(m[4], 0x88);
    EXPECT_EQ(m[8], 0xCC);
    EXPECT_EQ(Tang()->Q2Core()->GetStats().badWords, 0u);

    std::vector<uint8_t> foreign = q2Bytes;
    foreign[0] ^= 1;
    EXPECT_FALSE(load(foreign));
}

/// What the ESP's TTD keyframes rely on (ttd-design.md §11): core_load / core_back move the epoch, the firmware's own
/// writes are counted (the app's are not), the replay feed writes the core alone, and the Live part of a checkpoint
/// takes the Evo side and checks the core the replay computed
TEST_F(Vdac4Tang_Test, Quake2ReplayFeedEpochAndLivePart)
{
    PastEspBoot();
    const uint64_t epoch = Tang()->Q2Epoch();
    uint32_t version = 0;
    ASSERT_EQ(Tang()->LoadCore(Vdac4BomzhCore::kUserCode, version), 0);
    EXPECT_NE(Tang()->Q2Epoch(), epoch) << "core_load";

    // The firmware's write counts, the app's does not
    const uint64_t outside = Tang()->Q2OutsideInputs();
    Tang()->FirmwareLinkWrite(0x40000, 0x0A00'0000u);
    EXPECT_EQ(Tang()->Q2OutsideInputs(), outside + 1);
    for (uint32_t w : {0x10000u, 0x20000u, 0x30000u, (8u << 16) | 16u, 0x40000u, 0u, 0x50000u, 0x60000u})
        Tang()->LinkWrite(0x40000, w);
    const uint8_t ftWord[4] = {1, 2, 3, 4};
    Tang()->LinkFtWrite(0x1000, ftWord, 1);
    EXPECT_EQ(Tang()->Q2OutsideInputs(), outside + 1);
    EXPECT_EQ(Tang()->Q2Core()->GetStats().badWords, 0u) << "SETUP whole, FT words not core words";

    // A checkpoint's Live part, then the core moved on by the replay feed alone (the Tang's own state unchanged)
    auto live = [&] {
        std::vector<uint8_t> bytes;
        Vdac4EspStateWriter w(bytes);
        Tang()->SaveQ2(w, Vdac4Tang::Q2Part::Live);
        return bytes;
    };
    auto whole = [&] {
        std::vector<uint8_t> bytes;
        Vdac4EspStateWriter w(bytes);
        Tang()->SaveQ2(w);
        return bytes;
    };
    const std::vector<uint8_t> before = whole();
    const Vdac4Tang::State tangBefore = Tang()->GetState();
    const uint64_t epochBefore = Tang()->Q2Epoch();
    for (uint32_t w : {0x0600'0000u, 0u, (8u << 16) | 16u, 9u, 0x0700'0000u, 0u})
        Tang()->Q2ReplayLinkWrite(0x40000, w);
    const uint8_t words[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Tang()->Q2ReplayLinkFtWrite(0x2000, words, 2);
    EXPECT_EQ(Tang()->Q2OutsideInputs(), outside + 1) << "the replay feed is no outside input";
    EXPECT_EQ(Tang()->Q2Epoch(), epochBefore);
    EXPECT_EQ(Tang()->Q2Core()->Frames(), 1u);
    EXPECT_EQ(Tang()->GetState().ftAddr, tangBefore.ftAddr) << "the Tang itself unchanged";
    EXPECT_EQ(Tang()->GetState().scratch, tangBefore.scratch);
    Tang()->SetShowing(true);  // the Evo side moves too
    const std::vector<uint8_t> after = live();
    const std::vector<uint8_t> afterWhole = whole();

    // Back to the core before the feed; the same feed again gives the recorded core: the Live part matches
    Vdac4EspStateReader back(before.data(), before.size());
    ASSERT_TRUE(Tang()->LoadQ2(back));
    bool matches = true;
    Vdac4EspStateReader early(after.data(), after.size());
    ASSERT_TRUE(Tang()->LoadQ2(early, Vdac4Tang::Q2Part::Live, &matches));
    EXPECT_FALSE(matches) << "the core is not where the checkpoint was";
    Vdac4EspStateReader again(before.data(), before.size());
    ASSERT_TRUE(Tang()->LoadQ2(again));
    for (uint32_t w : {0x0600'0000u, 0u, (8u << 16) | 16u, 9u, 0x0700'0000u, 0u})
        Tang()->Q2ReplayLinkWrite(0x40000, w);
    Tang()->Q2ReplayLinkFtWrite(0x2000, words, 2);
    Vdac4EspStateReader at(after.data(), after.size());
    ASSERT_TRUE(Tang()->LoadQ2(at, Vdac4Tang::Q2Part::Live, &matches));
    EXPECT_TRUE(at.AtEnd());
    EXPECT_TRUE(matches);
    EXPECT_EQ(whole(), afterWhole) << "the Evo side came from the Live part";

    ASSERT_TRUE(Tang()->CoreBack());
    EXPECT_EQ(Tang()->Q2Core(), nullptr);
    const uint64_t epochBack = Tang()->Q2Epoch();
    Tang()->Q2ReplayLinkWrite(0x40000, 0x0700'0000u);  // no core: nothing
    EXPECT_EQ(Tang()->Q2Epoch(), epochBack);
}

#endif // ENABLE_VDAC2
