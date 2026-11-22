// The host-side IN (#0F) on each machine, with and without NeoGS (gs-debugger design §8A.10, requirements §4.16 "The
// host-side IN (#0F)")

#include "pch.h"

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/sound/soundmanager.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/testtier.h"

/// The NedoOS kernel's NeoGS SD driver (INSTSDD, kernel/ngssddrv.asm) and `nc` read the host port #000F, NeoGS's
/// card-side GSCFG0 address, and take anything but #FF for a NeoGS. The card does not answer it: the NeoGS FPGA
/// decodes only #B3, #BB and #33 on the ZX bus (ngs fpga/current/zxbus/zxbus.v:217-223), so the value is the machine's
class GSHostPort0F_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    /// IN (#000F) on `model` after two frames, with `kind` in the GS slot as a config would fit it (NONE: no card)
    int Read(const char* model, GSTypeKind kind)
    {
        std::shared_ptr<Emulator> emulator = _manager->CreateEmulatorWithModel(
            "port0f", model, LoggerLevel::LogError, nullptr, [kind](CONFIG& config) { config.sound.gsTypeKind = kind; });
        if (!emulator)
            return -1;
        EmulatorContext* context = emulator->GetContext();
        int value = -2;
        if (context->pSoundManager->fittedGeneralSoundKind() == kind)
        {
            emulator->RunNFrames(2, true);
            value = context->pCore->GetZ80()->in(0x000F);
        }
        _manager->RemoveEmulator(emulator->GetId());
        return value;
    }
};

/// What each machine answers right after power-up, the same without a card, with NeoGS and with the classic GS:
///   - #FF where nothing decodes #0F: Pentagon, ZX-Evo (BaseConf, TS-Conf), ATM Turbo 2 / 2+, Scorpion; the 128K's
///     floating bus reads #FF in the border, where the read falls;
///   - the Pentagon 2.666 powers up in DOS (PaE.vhd: dos = '1'), where its Beta Disk emulator takes every odd port by
///     A7..5 (PaE.vhd:1530-1534): #0F is the VG93's status, #25. The NedoOS kernel's INSTSDD reads it so (DOS on, PC
///     #10D0 in GSNedoOsP2666_Test.V5KernelDrivesTheNeoGsSdCard) and takes NEOGSINIT with or without a card; without
///     one, its wait for the #F3 reply ends the probe;
///   - the Profi powers up in its SYS ROM with the DOS latch on: #0F is the VG93's status too, #84.
/// So the NedoOS test "not #FF" never finds a NeoGS through this port: outside DOS every machine reads #FF
namespace
{
struct Port0FCase
{
    const char* model;
    int value;
};

constexpr int kNotRead = -3;

void ExpectSame(const Port0FCase& c, int none, int ngs, int gs)
{
    EXPECT_EQ(none, c.value) << c.model << ", no card";
    EXPECT_EQ(ngs, c.value) << c.model << ", NeoGS";
    if (gs != kNotRead)
        EXPECT_EQ(gs, c.value) << c.model << ", the classic GS";
}
}  // namespace

TEST_F(GSHostPort0F_Test, NoCardAnswersIt)
{
    // The NeoGS machines of NedoOS: the ZX-Evo and the Pentagon 2.666
    for (const Port0FCase& c : {Port0FCase{"ATM3", 0xFF}, Port0FCase{"P2666", 0x25}})
        ExpectSame(c, Read(c.model, GSTypeKind::NONE), Read(c.model, GSTypeKind::NGS), kNotRead);
}

TEST_F(GSHostPort0F_Test, EveryMachineWithEveryCard)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "UNREAL_LONG_TESTS=1: every machine, three cards";
    for (const Port0FCase& c : {Port0FCase{"PENTAGON", 0xFF}, Port0FCase{"P2666", 0x25}, Port0FCase{"ATM3", 0xFF},
                                Port0FCase{"TSL", 0xFF}, Port0FCase{"ATM710", 0xFF}, Port0FCase{"ATM450", 0xFF},
                                Port0FCase{"128k", 0xFF}, Port0FCase{"SCORPION", 0xFF}, Port0FCase{"PROFI", 0x84}})
        ExpectSame(c, Read(c.model, GSTypeKind::NONE), Read(c.model, GSTypeKind::NGS), Read(c.model, GSTypeKind::Z80));
}
