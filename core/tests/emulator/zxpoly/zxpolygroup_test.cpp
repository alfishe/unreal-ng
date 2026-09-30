#include "stdafx.h"
#include "pch.h"

#include "_helpers/testpathhelper.h"
#include "common/image/imagehelper.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/emulator.h"
#include "emulator/mainloop.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "emulator/emulatormanager.h"
#include "emulator/media/modelswitch.h"
#include <algorithm>
#include <cstring>
#include "emulator/memory/memory.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"
#include "emulator/zxpoly/zxpolyscreencomposer.h"
#include "emulator/video/screen.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "3rdparty/message-center/messagecenter.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

/// ZX-Poly group on stock models: the .zxp corpus runs in lockstep on four
/// instances. Boot-bound: every case runs hundreds of emulated frames on four
/// machines, so it is well over the 50 ms guideline by design.
///
/// Set ZXPOLY_DUMP_PNG=1 to write the composed frames to
/// scratch/zxpoly/<model>/<title>-<frame>.png for visual review.
class ZXPolyGroup_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void TearDown() override
    {
        _group.reset();
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void CreateGroup(const std::string& model)
    {
        _group = std::make_unique<ZXPolyGroup>("zxpoly-test");
        std::string error;
        ASSERT_TRUE(_group->Create(model, &error)) << error;
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            _group->GetInstance(m)->EnableTurboMode();
    }

    /// Runs frames, checking lockstep after each; returns frames completed in lockstep
    unsigned RunInLockstep(unsigned frames, ZXPolyGroup::Divergence& divergence)
    {
        for (unsigned f = 0; f < frames; f++)
        {
            _group->RunFrame();
            divergence = _group->CheckLockstep();
            if (divergence.diverged)
                return f;
        }
        return frames;
    }

    void DumpPng(const std::string& model, const std::string& title, unsigned frame)
    {
        if (std::getenv("ZXPOLY_DUMP_PNG") == nullptr)
            return;
        auto save = [&](const std::string& suffix) {
            std::vector<uint32_t> picture;
            _group->Compose(picture);
            const std::string path = TestPathHelper::GetTestScratchPath(
                "zxpoly/" + model + "/" + title + "-" + std::to_string(frame) + suffix + ".png");
            ImageHelper::SavePNG(path, reinterpret_cast<uint8_t*>(picture.data()),
                                 picture.size() * sizeof(uint32_t), ZXPolyScreenComposer::OUT_WIDTH,
                                 ZXPolyScreenComposer::OUT_HEIGHT);
        };
        save("");

        // The same frame as a stock Spectrum shows it: the master alone (mode 0)
        const uint8_t mode = _group->GetVideoMode();
        _group->SetVideoMode(0);
        save("-classic");
        _group->SetVideoMode(mode);
    }

    std::unique_ptr<ZXPolyGroup> _group;
};

class ZXPolyGroupCorpus_Test : public ZXPolyGroup_Test,
                               public ::testing::WithParamInterface<std::tuple<std::string, std::string>>
{
};

TEST_P(ZXPolyGroupCorpus_Test, ZxpRunsInLockstep)
{
    const std::string model = std::get<0>(GetParam());
    const std::string file = std::get<1>(GetParam());
    const std::string title = file.substr(0, file.find('.'));

    CreateGroup(model);
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/" + file), &error)) << error;

    // Right after the load the four modules hold identical registers
    ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    ASSERT_FALSE(divergence.diverged) << "after load: module " << divergence.module << ": " << divergence.what;

    DumpPng(model, title, 0);

    constexpr unsigned FRAMES = 250;    // 5 s of emulated time
    const unsigned done = RunInLockstep(FRAMES, divergence);
    DumpPng(model, title, done);

    EXPECT_EQ(done, FRAMES) << title << " on " << model << ": module " << divergence.module << " left lockstep at frame "
                            << done << ": " << divergence.what;
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, ZXPolyGroupCorpus_Test,
    ::testing::Combine(::testing::Values(std::string("128k"), std::string("PENTAGON")),
                       ::testing::Values(std::string("Alien8.zxp"), std::string("buratino_adventures.zxp"),
                                         std::string("ComandoQuatro.zxp"), std::string("flyshark.zxp"),
                                         std::string("OFCZXPOLY.zxp"), std::string("SummerSanta2022.zxp"),
                                         std::string("fh.zxp"))),
    [](const ::testing::TestParamInfo<std::tuple<std::string, std::string>>& info) {
        std::string file = std::get<1>(info.param);
        std::string name = std::get<0>(info.param) + "_" + file.substr(0, file.find('.'));
        for (char& c : name)
            if (!isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return name;
    });

class ZXPolyGroupModels_Test : public ZXPolyGroup_Test, public ::testing::WithParamInterface<std::string>
{
};

/// Stock snapshot on the master, replicated into the slaves: identical planes,
/// so the group must stay in lockstep indefinitely (the calibration state)
TEST_P(ZXPolyGroupModels_Test, ReplicatedSnapshotStaysInLockstep)
{
    CreateGroup(GetParam());
    ASSERT_TRUE(_group->GetInstance(0)->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/Dizzy X.sna")));

    _group->ReplicateFromMaster();
    ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    ASSERT_FALSE(divergence.diverged) << divergence.what;

    ZXPolyGroup::Divergence afterRun;
    constexpr unsigned FRAMES = 200;
    const unsigned done = RunInLockstep(FRAMES, afterRun);
    EXPECT_EQ(done, FRAMES) << "module " << afterRun.module << " at frame " << done << ": " << afterRun.what;
}

INSTANTIATE_TEST_SUITE_P(Models, ZXPolyGroupModels_Test, ::testing::Values(std::string("128k"), std::string("PENTAGON")),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                             return info.param == "128k" ? std::string("Spectrum128") : info.param;
                         });

/// Gameplay: a deterministic input script (menu keys, then Sinclair/QAOP
/// directions and fire) drives every title past its menu; the group must stay
/// in lockstep with keys applied at frame boundaries to all four instances.
/// Boot-bound (1500 frames on four machines per case).
TEST_P(ZXPolyGroupCorpus_Test, ScriptedPlayStaysInLockstep)
{
    const std::string model = std::get<0>(GetParam());
    const std::string file = std::get<1>(GetParam());
    const std::string title = file.substr(0, file.find('.')) + "-play";

    CreateGroup(model);
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/" + file), &error)) << error;
    if (std::getenv("ZXPOLY_PORT_CHECK") != nullptr)
        _group->EnablePortReadCheck(true);

    // Menu keys first (keyboard / start selections), then play keys
    static const ZXKeysEnum menuKeys[] = {ZXKEY_1, ZXKEY_ENTER, ZXKEY_0, ZXKEY_SPACE, ZXKEY_1, ZXKEY_ENTER};
    static const ZXKeysEnum playKeys[] = {ZXKEY_Q, ZXKEY_A, ZXKEY_O, ZXKEY_P, ZXKEY_M, ZXKEY_SPACE,
                                          ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9, ZXKEY_0, ZXKEY_Z, ZXKEY_X};

    uint32_t rng = 0x2A5F17u;   // fixed seed: the same script every run
    auto next = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 16;
    };

    constexpr unsigned FRAMES = 1500;   // 30 s of emulated time
    ZXPolyGroup::Divergence divergence;
    ZXKeysEnum held = ZXKEY_NONE;
    unsigned done = 0;
    for (unsigned f = 0; f < FRAMES; f++)
    {
        // Every 10 frames: release the held key; press the next one for 6 frames
        if (f % 10 == 0)
        {
            if (held != ZXKEY_NONE)
                _group->ReleaseKey(held);
            held = f < 600 ? menuKeys[(f / 100) % std::size(menuKeys)] : playKeys[next() % std::size(playKeys)];
            _group->PressKey(held);
        }
        else if (f % 10 == 6 && held != ZXKEY_NONE)
        {
            _group->ReleaseKey(held);
            held = ZXKEY_NONE;
        }

        _group->RunFrame();
        divergence = _group->CheckLockstep();
        if (divergence.diverged)
            break;
        done = f + 1;
        if (done % 250 == 0)
            DumpPng(model, title, done);
    }

    DumpPng(model, title, done);
    EXPECT_EQ(done, FRAMES) << title << " on " << model << ": module " << divergence.module
                            << " left lockstep at frame " << done << ": " << divergence.what;
}

struct ZXPolyDiskCase
{
    const char* model;
    const char* file;
    uint8_t expectedMode;
};

class ZXPolyGroupDisk_Test : public ZXPolyGroup_Test, public ::testing::WithParamInterface<ZXPolyDiskCase>
{
};

/// The loader path: a TR-DOS disk with a ZX-Poly multiloader boots on the
/// master alone, streams the planes into the parked slaves through the #3D00
/// IO window, then locks with a local reset (SETPOLYMAIN). After the lock the
/// four run in lockstep. Boot-bound: TR-DOS loading takes thousands of frames
TEST_P(ZXPolyGroupDisk_Test, MultiloaderBootsAndRunsInLockstep)
{
    const ZXPolyDiskCase& param = GetParam();
    const std::string title = std::string(param.file).substr(0, std::string(param.file).find('.')) + "-disk";

    CreateGroup(param.model);
    std::string error;
    ASSERT_TRUE(_group->BootDisk(TestPathHelper::GetTestDataPath(std::string("machines/zxpoly/trd/") + param.file),
                                 &error))
        << error;

    unsigned bootFrames = 0;
    constexpr unsigned MAX_BOOT_FRAMES = 6000;
    while (!_group->IsLocked() && bootFrames < MAX_BOOT_FRAMES)
    {
        _group->RunFrame();
        bootFrames++;
    }
    DumpPng(param.model, title, 0);
    ASSERT_TRUE(_group->IsLocked()) << "no SETPOLYMAIN lock after " << bootFrames << " frames";
    if (std::getenv("ZXPOLY_M1_TRACE") != nullptr)
        _group->EnableInstructionTrace(true);

    EXPECT_EQ(_group->GetVideoMode(), param.expectedMode);
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
        EXPECT_GT(_group->GetOverlayBytes(m), 10000u) << "module " << m << " received no plane data";

    // Input script after the lock: the start key, then play keys (fixed seed)
    static const ZXKeysEnum playKeys[] = {ZXKEY_Q, ZXKEY_A, ZXKEY_O, ZXKEY_P, ZXKEY_M, ZXKEY_SPACE, ZXKEY_Z,
                                          ZXKEY_X, ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9, ZXKEY_0, ZXKEY_E};
    uint32_t rng = 0x51D0u;
    ZXKeysEnum held = ZXKEY_NONE;

    ZXPolyGroup::Divergence divergence;
    constexpr unsigned FRAMES = 1000;
    unsigned done = 0;
    for (; done < FRAMES; done++)
    {
        if (done >= 100 && done % 8 == 0)
        {
            if (held != ZXKEY_NONE)
                _group->ReleaseKey(held);
            rng = rng * 1664525u + 1013904223u;
            held = done < 140 ? ZXKEY_1 : playKeys[(rng >> 16) % std::size(playKeys)];
            _group->PressKey(held);
        }

        _group->RunFrame();
        divergence = _group->CheckLockstep();
        if (divergence.diverged)
            break;
        if ((done + 1) % 200 == 0)
            DumpPng(param.model, title, done + 1);
    }
    EXPECT_EQ(done, FRAMES) << title << ": module " << divergence.module << " left lockstep " << done
                            << " frames after the lock (boot took " << bootFrames << "): " << divergence.what;
}

INSTANTIATE_TEST_SUITE_P(Loaders, ZXPolyGroupDisk_Test,
                         ::testing::Values(ZXPolyDiskCase{"PENTAGON", "atw2.trd", 4},
                                           ZXPolyDiskCase{"PENTAGON", "zxword.trd", 5}),
                         [](const ::testing::TestParamInfo<ZXPolyDiskCase>& info) {
                             std::string name = std::string(info.param.model) + "_" + info.param.file;
                             for (char& c : name)
                                 if (!isalnum(static_cast<unsigned char>(c)))
                                     c = '_';
                             return name;
                         });

/// A system reset of the master is a ZX-Poly system RESET: #3D00 back to 0,
/// ports unlocked, slaves parked, only CPU0 shown (mode 0). Afterwards the
/// composed picture is exactly the master's classic screen
TEST_P(ZXPolyGroupModels_Test, MasterResetReturnsToLoaderPhase)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error)) << error;
    _group->RunFrames(20);
    ASSERT_TRUE(_group->IsLocked());

    _group->GetInstance(0)->Reset();
    _group->RunFrames(5);

    EXPECT_FALSE(_group->IsLocked());
    EXPECT_EQ(_group->GetVideoMode(), 0);
    EXPECT_EQ(_group->GetPort3D00(), 0);

    // Mode 0 composes CPU0 alone: slave screens (still the game) must not show
    std::vector<uint32_t> composed;
    _group->Compose(composed);
    std::array<const uint8_t*, 4> masterOnly;
    masterOnly.fill(_group->GetScreenMemory(0));
    std::vector<uint32_t> expected(composed.size());
    uint32_t palette[16];
    _group->GetContext(0)->pScreen->GetRGBAPalette16(palette);
    const bool flash = ((_group->GetContext(0)->emulatorState.frame_counter >> 4) & 1u) != 0;
    ZXPolyScreenComposer::Compose(masterOnly, 0, flash, palette, expected.data());
    EXPECT_EQ(composed, expected);
}

/// The ZX-Poly Test ROM (zxpolytest.prom, the platform's own self-test and
/// demo) - the coupled machine: the slaves are released by #3D00 D0 before any
/// lock, reset one by one with injected JP commands (R0 D5), run their own
/// test code, HALT, and report through the IO window; CPU0 resets itself, RAM0
/// is mapped at #0000. Every check must print OK, then the mode 4 and mode 5
/// demo pictures are streamed into the slaves. Boot-bound (ROM tests + ZX0
/// unpacking run for hundreds of frames)
TEST_P(ZXPolyGroupModels_Test, TestRomPassesAllChecks)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadPROM(TestPathHelper::GetTestDataPath("machines/zxpoly/rom/zxpolytest.prom"), &error))
        << error;

    const std::string master = _group->GetInstance(0)->GetUUID();
    auto screen = [&]() { return ScreenOCR::ocrScreen(master); };

    unsigned frames = 0;
    while (screen().find("PRESS ANY KEY") == std::string::npos && frames < 3000)
    {
        _group->RunFrame();
        frames++;
    }
    const std::string text = screen();
    DumpPng(GetParam(), "testrom-checks", frames);
    ASSERT_NE(text.find("PRESS ANY KEY"), std::string::npos) << "after " << frames << " frames:\n" << text;
    EXPECT_EQ(text.find("BAD"), std::string::npos) << text;
    EXPECT_EQ(text.find("NON"), std::string::npos) << text;
    for (const char* check : {"CPU0", "CPU1", "CPU2", "CPU3", "RAM0"})
        EXPECT_NE(text.find(check), std::string::npos) << check << " missing:\n" << text;

    // Mode 4 demo: four ZX0-packed planes streamed through the IO window
    _group->PressKey(ZXKEY_SPACE);
    _group->RunFrames(5);
    _group->ReleaseKey(ZXKEY_SPACE);
    _group->RunFrames(400);
    EXPECT_EQ(_group->GetVideoMode(), 4);
    DumpPng(GetParam(), "testrom-mode4", frames);

    // Mode 5 demo (512 x 384)
    _group->PressKey(ZXKEY_SPACE);
    _group->RunFrames(5);
    _group->ReleaseKey(ZXKEY_SPACE);
    _group->RunFrames(400);
    EXPECT_EQ(_group->GetVideoMode(), 5);
    DumpPng(GetParam(), "testrom-mode5", frames);
}

/// Platform mechanisms before the lock, each driven by a small Z80 program
/// in the master and in CPU1 (the programs poke the platform ports exactly
/// as ZX-Poly software does). Pentagon: no contention, no floating bus
class ZXPolyPlatform_Test : public ZXPolyGroup_Test
{
protected:
    // Z80 opcodes used by the programs
    static constexpr uint8_t LD_BC = 0x01, LD_A = 0x3E, LD_HL = 0x21, LD_B = 0x06, LD_NN_A = 0x32;
    static constexpr uint8_t ED = 0xED, OUT_C_A = 0x79, LD_I_A = 0x47, IM2 = 0x5E;
    static constexpr uint8_t DI = 0xF3, EI = 0xFB, HALT = 0x76, JR = 0x18, DJNZ = 0x10, INC_HL_M = 0x34;

    void SetUp() override
    {
        ZXPolyGroup_Test::SetUp();
        CreateGroup("PENTAGON");
        StartMaster(0x8000);
    }

    Memory& Mem(size_t module) { return *_group->GetContext(module)->pMemory; }
    Z80& Cpu(size_t module) { return *_group->GetContext(module)->pCore->GetZ80(); }

    void Poke(size_t module, uint16_t address, const std::vector<uint8_t>& bytes)
    {
        for (size_t i = 0; i < bytes.size(); i++)
            Mem(module).DirectWriteToZ80Memory(static_cast<uint16_t>(address + i), bytes[i]);
    }

    uint8_t Peek(size_t module, uint16_t address) { return Mem(module).DirectReadFromZ80Memory(address); }

    void StartMaster(uint16_t pc)
    {
        Z80& cpu = Cpu(0);
        cpu.pc = pc;
        cpu.sp = 0x7F00;
        cpu.iff1 = cpu.iff2 = 0;
        cpu.halted = 0;
    }

    /// OUT (port), value through BC
    static std::vector<uint8_t> Out(uint16_t port, uint8_t value)
    {
        return {LD_BC, static_cast<uint8_t>(port & 0xFF), static_cast<uint8_t>(port >> 8), LD_A, value, ED, OUT_C_A};
    }

    static void Append(std::vector<uint8_t>& code, const std::vector<uint8_t>& more)
    {
        code.insert(code.end(), more.begin(), more.end());
    }

    /// Master program prologue: CPU1's reset command = JP target, slaves
    /// released (#3D00 D0), CPU1 reset (R0 = #22: reset, heap window 2)
    static std::vector<uint8_t> ResetCpu1To(uint16_t target, uint8_t main3D00 = 0x01)
    {
        std::vector<uint8_t> code;
        Append(code, Out(0x12FF, static_cast<uint8_t>(target & 0xFF)));
        Append(code, Out(0x13FF, static_cast<uint8_t>(target >> 8)));
        Append(code, Out(0x11FF, 0xC3));
        Append(code, Out(0x3D00, main3D00));
        Append(code, Out(0x10FF, 0x22));
        return code;
    }

    /// IM 2 through a vector table at #91FF (the bus reads #FF) -> handler
    static std::vector<uint8_t> Im2(uint16_t handler)
    {
        (void)handler;
        return {LD_A, 0x91, ED, LD_I_A, ED, IM2};
    }

    void VectorTable(size_t module, uint16_t handler)
    {
        Poke(module, 0x91FF, {static_cast<uint8_t>(handler & 0xFF), static_cast<uint8_t>(handler >> 8)});
    }

    /// Marker handler: LD A,value; LD (address),A; DI; HALT
    void Marker(size_t module, uint16_t at, uint16_t address, uint8_t value)
    {
        Poke(module, at, {LD_A, value, LD_NN_A, static_cast<uint8_t>(address & 0xFF), static_cast<uint8_t>(address >> 8), DI, HALT});
    }
};

TEST_F(ZXPolyPlatform_Test, SlaveMissesFrameIntBeforeLockAndTakesLocalInt)
{
    // CPU1: IM 2, EI, HALT - only an INT gets it to the marker handler
    std::vector<uint8_t> slave = Im2(0x8200);
    Append(slave, {EI, HALT, JR, 0xFD});
    Poke(1, 0x8100, slave);
    VectorTable(1, 0x8200);
    Marker(1, 0x8200, 0x9000, 0xAA);
    Poke(1, 0x9000, {0x00});

    std::vector<uint8_t> master = ResetCpu1To(0x8100);
    Append(master, {JR, 0xFE});
    Poke(0, 0x8000, master);

    _group->RunFrames(10);   // ten frame INTs a stock CPU would have taken
    EXPECT_EQ(Peek(1, 0x9000), 0x00) << "the common frame INT reached a slave before the lock";

    // R0 D7: a local INT for CPU1 (heap window 2 kept)
    std::vector<uint8_t> interrupt = Out(0x10FF, 0x82);
    Append(interrupt, {JR, 0xFE});
    Poke(0, 0x8100, interrupt);
    StartMaster(0x8100);
    _group->RunFrames(2);
    EXPECT_EQ(Peek(1, 0x9000), 0xAA) << "the local INT did not reach CPU1";
}

TEST_F(ZXPolyPlatform_Test, WindowWriteSendsNmiUnlessR1MasksIt)
{
    // CPU1 maps RAM0 over the ROM (#7FFD D6, unlocked) so #0066 is its own
    // NMI handler, then DI + HALT: only an NMI gets it to the marker
    std::vector<uint8_t> slave = Out(0x7FFD, 0x40);
    Append(slave, {DI, HALT, JR, 0xFD});
    Poke(1, 0x8100, slave);
    Poke(1, 0x9001, {0x00});
    uint8_t* ram0 = Mem(1).RAMPageAddress(0);
    const uint8_t handler[] = {LD_A, 0xBB, LD_NN_A, 0x01, 0x90, DI, HALT};
    std::memcpy(ram0 + 0x66, handler, sizeof(handler));

    // Master: CPU1 reset + running, then map CPU1 (#3D00 = #21) and write
    // one byte through the window
    std::vector<uint8_t> master = ResetCpu1To(0x8100);
    Append(master, {LD_B, 0x00, DJNZ, 0xFE});   // let CPU1 reach its HALT
    Append(master, Out(0x3D00, 0x21));
    Append(master, Out(0x9100, 0x5A));
    Append(master, Out(0x3D00, 0x01));
    Append(master, {JR, 0xFE});
    Poke(0, 0x8000, master);

    _group->RunFrames(3);
    EXPECT_EQ(Peek(1, 0x9100), 0x5A) << "the window write did not land in CPU1";
    EXPECT_EQ(Peek(1, 0x9001), 0xBB) << "the window write did not send CPU1 an NMI";

    // Same again with CPU1's R1 D4 set: NMI masked
    Poke(1, 0x9001, {0x00});
    Poke(1, 0x8200, {DI, HALT});
    std::vector<uint8_t> masked = ResetCpu1To(0x8200);
    Append(masked, Out(0x11FF, 0x10));
    Append(masked, {LD_B, 0x00, DJNZ, 0xFE});
    Append(masked, Out(0x3D00, 0x21));
    Append(masked, Out(0x9100, 0x5B));
    Append(masked, Out(0x3D00, 0x01));
    Append(masked, {JR, 0xFE});
    Poke(0, 0x8000, masked);
    StartMaster(0x8000);
    _group->RunFrames(3);
    EXPECT_EQ(Peek(1, 0x9100), 0x5B);
    EXPECT_EQ(Peek(1, 0x9001), 0x00) << "R1 D4 did not mask the NMI";
}

TEST_F(ZXPolyPlatform_Test, HaltNotificationWakesTheMasterWhoseFrameIntIsGated)
{
    // CPU1: DI, a delay, HALT - its R1 = #41 sends an INT to CPU0 on the HALT
    Poke(1, 0x8100, {DI, LD_B, 0x00, DJNZ, 0xFE, DJNZ, 0xFE, HALT});

    // Master: IM 2 marker handler; #7FFD D7 gates its frame INT while
    // unlocked, so only the notification can wake it from EI + HALT
    std::vector<uint8_t> master = ResetCpu1To(0x8100);
    Append(master, Out(0x11FF, 0x41));
    Append(master, Im2(0x8200));
    Append(master, Out(0x7FFD, 0x80));
    Append(master, {EI, HALT, JR, 0xFD});
    Poke(0, 0x8000, master);
    VectorTable(0, 0x8200);
    Marker(0, 0x8200, 0x9002, 0xCC);
    Poke(0, 0x9002, {0x00});

    _group->RunFrames(5);
    EXPECT_EQ(Peek(0, 0x9002), 0xCC) << "CPU1's HALT did not interrupt CPU0";

    // Control: without the notification the gated master sleeps on
    Poke(0, 0x9002, {0x00});
    std::vector<uint8_t> control = ResetCpu1To(0x8100);
    Append(control, Im2(0x8200));
    Append(control, Out(0x7FFD, 0x80));
    Append(control, {EI, HALT, JR, 0xFD});
    Poke(0, 0x8000, control);
    StartMaster(0x8000);
    _group->RunFrames(5);
    EXPECT_EQ(Peek(0, 0x9002), 0x00) << "#7FFD D7 did not gate the master's frame INT";
}

TEST_F(ZXPolyPlatform_Test, StopAddressParksASlaveUntilMoved)
{
    // CPU1: LD HL,#9000 ; loop: INC (HL) ; JR loop  (INC at #8103)
    Poke(1, 0x8100, {LD_HL, 0x00, 0x90, INC_HL_M, JR, 0xFD});
    Poke(1, 0x9000, {0x00});

    // Master: start CPU1, then stop address = #8103 (R2, R3)
    std::vector<uint8_t> master = ResetCpu1To(0x8100);
    Append(master, Out(0x12FF, 0x03));
    Append(master, Out(0x13FF, 0x81));
    Append(master, {JR, 0xFE});
    Poke(0, 0x8000, master);

    _group->RunFrames(3);
    const uint8_t parked = Peek(1, 0x9000);
    _group->RunFrames(3);
    EXPECT_EQ(Peek(1, 0x9000), parked) << "CPU1 did not stop at its stop address";
    EXPECT_EQ(Cpu(1).pc, 0x8103);

    // A new stop address releases it
    std::vector<uint8_t> move = Out(0x13FF, 0x82);
    Append(move, {JR, 0xFE});
    Poke(0, 0x8100, move);
    StartMaster(0x8100);
    _group->RunFrames(2);
    EXPECT_NE(Peek(1, 0x9000), parked) << "moving the stop address did not release CPU1";
}

TEST_F(ZXPolyPlatform_Test, SlaveDeviceWritesReachTheMachineUnlessDisabled)
{
    // CPU1: OUT (#FE),5 (cyan border) then DI; HALT
    std::vector<uint8_t> slave = Out(0x00FE, 0x05);
    Append(slave, {DI, HALT});
    Poke(1, 0x8100, slave);

    std::vector<uint8_t> master = Out(0x00FE, 0x00);
    Append(master, ResetCpu1To(0x8100));
    Append(master, {JR, 0xFE});
    Poke(0, 0x8000, master);
    _group->RunFrames(2);
    EXPECT_EQ(_group->GetContext(0)->emulatorState.pFE & 0x07, 0x05) << "CPU1's border write did not reach the machine";

    // R0 D4: CPU1's IO writes disabled (R0 = #32: reset + window 2 + D4)
    std::vector<uint8_t> disabled = Out(0x00FE, 0x00);
    Append(disabled, Out(0x12FF, 0x00));
    Append(disabled, Out(0x13FF, 0x81));
    Append(disabled, Out(0x11FF, 0xC3));
    Append(disabled, Out(0x10FF, 0x32));
    Append(disabled, {JR, 0xFE});
    Poke(0, 0x8000, disabled);
    StartMaster(0x8000);
    _group->RunFrames(2);
    EXPECT_EQ(_group->GetContext(0)->emulatorState.pFE & 0x07, 0x00) << "R0 D4 did not disable CPU1's device writes";
}

/// 128K floating bus: an IN from an undecoded port returns the video byte on
/// the bus - on ZX-Poly the one CPU0's plane supplies. A game that waits for
/// a bus value must see the same value on every module, or the slaves leave
/// the loop at a different moment and split off
/// Floating-bus probe: every module loops on IN A,(#FF) until it reads #AA,
/// then marks #9003. The master's plane never holds #AA, the slaves' planes
/// are all #AA: a slave that fetched from its own plane would escape the loop
static void SetUpFloatingBusProbe(ZXPolyGroup& group)
{
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        group.GetContext(m)->config.floatbus = 1;    // the ini default is off
    EmulatorContext* master = group.GetContext(0);
    Memory& memory = *master->pMemory;

    // loop: IN A,(#FF) ; CP #AA ; JR NZ,loop ; LD A,1 ; LD (#9003),A ; DI ; HALT
    const uint8_t code[] = {0xDB, 0xFF, 0xFE, 0xAA, 0x20, 0xFA, 0x3E, 0x01, 0x32, 0x03, 0x90, 0xF3, 0x76};
    for (size_t i = 0; i < sizeof(code); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    memory.DirectWriteToZ80Memory(0x9003, 0x00);
    std::memset(memory.RAMPageAddress(5), 0x00, 6912);    // master plane: never #AA
    Z80& cpu = *master->pCore->GetZ80();
    cpu.pc = 0x8000;
    cpu.sp = 0x7F00;
    cpu.iff1 = cpu.iff2 = 0;

    group.ReplicateFromMaster();
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
        std::memset(group.GetContext(m)->pMemory->RAMPageAddress(5), 0xAA, 6912);    // slave planes: #AA
}

TEST_P(ZXPolyGroupModels_Test, FloatingBusValueComesFromTheMaster)
{
    CreateGroup(GetParam());
    SetUpFloatingBusProbe(*_group);

    ZXPolyGroup::Divergence divergence;
    const unsigned done = RunInLockstep(50, divergence);
    EXPECT_EQ(done, 50u) << "module " << divergence.module << ": " << divergence.what;
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
        EXPECT_EQ(_group->GetContext(m)->pMemory->DirectReadFromZ80Memory(0x9003), 0x00) << "module " << m;
}

/// The same while the slaves' frame overlaps the master's next one (unlimited
/// speed): the master's floating-bus reads of the frame the slaves are still
/// running stay available to them
TEST_P(ZXPolyGroupModels_Test, FloatingBusValueComesFromTheMasterWhilePipelined)
{
    CreateGroup(GetParam());    // turbo on every member: unlimited speed
    SetUpFloatingBusProbe(*_group);

    unsigned overlapped = 0;
    EmulatorContext* master = _group->GetContext(0);
    uint64_t lastFrame = ~0ull;
    master->pCore->GetZ80()->busTraceHook = [&](char, uint16_t, uint8_t) {
        if (master->emulatorState.frame_counter != lastFrame)
        {
            lastFrame = master->emulatorState.frame_counter;
            overlapped += _group->IsPipelining() ? 1u : 0u;
        }
    };
    _group->RunFrames(50);
    master->pCore->GetZ80()->busTraceHook = nullptr;

    EXPECT_GT(overlapped, 40u) << "the slaves did not overlap the master";
    const ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    EXPECT_FALSE(divergence.diverged) << "module " << divergence.module << ": " << divergence.what;
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
        EXPECT_EQ(_group->GetContext(m)->pMemory->DirectReadFromZ80Memory(0x9003), 0x00) << "module " << m;
}

/// The group only synchronizes; the model decides what the machine has. A
/// stock 128K has no TR-DOS ROM (no Beta Disk), so a multiloader disk is
/// refused with a message instead of a half-started group
TEST_F(ZXPolyGroup_Test, DiskBootNeedsAModelWithTRDOS)
{
    CreateGroup("128k");
    std::string error;
    EXPECT_FALSE(_group->BootDisk(TestPathHelper::GetTestDataPath("machines/zxpoly/trd/atw2.trd"), &error));
    EXPECT_NE(error.find("TR-DOS"), std::string::npos) << error;
    EXPECT_FALSE(_group->IsLocked());
}

/// Slaves are hidden members: instance listings, index lookup and "most
/// recent" selection see only the master; the slaves stay reachable by ID
TEST_F(ZXPolyGroup_Test, SlavesAreHiddenFromInstanceListings)
{
    CreateGroup("PENTAGON");
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    auto listed = [&](size_t m) {
        return std::find(ids.begin(), ids.end(), _group->GetInstance(m)->GetId()) != ids.end();
    };
    EXPECT_TRUE(listed(0));
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
    {
        EXPECT_FALSE(listed(m)) << "module " << m;
        EXPECT_EQ(manager->GetEmulator(_group->GetInstance(m)->GetId()).get(), _group->GetInstance(m)) << "module " << m;
    }
    EXPECT_FALSE(manager->GetMostRecentEmulator()->IsHiddenGroupMember());
}

/// The picture is captured line by line as the beam passes (zxpoly renders a
/// line once the raster passed it), not read from memory at the frame end: a
/// screen-page switch in the middle of the frame shows as the top of the
/// picture from one page and the bottom from the other
TEST_P(ZXPolyGroupModels_Test, PictureFollowsTheBeamLineByLine)
{
    CreateGroup(GetParam());
    EmulatorContext* master = _group->GetContext(0);
    Memory& memory = *master->pMemory;

    // IM 2 (EI; RETI handler) ; loop: HALT ; screen 7 (black) ; wait into the
    // paper ; screen 5 (white) ; JR loop
    const std::vector<uint8_t> code = {
        0x3E, 0x91, 0xED, 0x47, 0xED, 0x5E, 0xFB,          // LD A,#91 ; LD I,A ; IM 2 ; EI
        0x76,                                              // loop: HALT
        0x01, 0xFD, 0x7F, 0x3E, 0x08, 0xED, 0x79,          // OUT (#7FFD),#08 - screen page 7
        0x16, 0x0C, 0x06, 0x00, 0x10, 0xFE, 0x15, 0x20, 0xF9, // LD D,12 ; LD B,0 ; DJNZ $ ; DEC D ; JR NZ
        0x01, 0xFD, 0x7F, 0xAF, 0xED, 0x79,                // OUT (#7FFD),#00 - screen page 5
        0x18, 0xE7};                                       // JR loop
    for (size_t i = 0; i < code.size(); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    memory.DirectWriteToZ80Memory(0x91FF, 0x00);
    memory.DirectWriteToZ80Memory(0x9200, 0x82);
    const uint8_t handler[] = {0xFB, 0xED, 0x4D};           // EI ; RETI
    for (size_t i = 0; i < sizeof(handler); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), handler[i]);

    std::memset(memory.RAMPageAddress(5), 0xFF, 6144);     // page 5: all ink
    std::memset(memory.RAMPageAddress(5) + 6144, 0x07, 768);
    std::memset(memory.RAMPageAddress(7), 0x00, 6144);     // page 7: all paper
    std::memset(memory.RAMPageAddress(7) + 6144, 0x07, 768);

    Z80& cpu = *master->pCore->GetZ80();
    cpu.pc = 0x8000;
    cpu.sp = 0x7F00;
    cpu.iff1 = cpu.iff2 = 0;
    _group->ReplicateFromMaster();
    _group->RunFrames(5);

    std::vector<uint32_t> picture;
    _group->Compose(picture);
    uint32_t palette[16];
    master->pScreen->GetRGBAPalette16(palette);
    auto at = [&](unsigned line) { return picture[(line * 2) * ZXPolyScreenComposer::OUT_WIDTH + 256]; };
    EXPECT_EQ(at(8), palette[0]) << "line 8 was fetched from page 7 (black)";
    EXPECT_EQ(at(185), palette[7]) << "line 185 was fetched from page 5 (white)";
}

/// The same at x2 host speed: the frame holds twice the CPU T-states and the
/// beam runs in base-clock T-states over all of it. The page switch comes
/// after ~53000 CPU T-states: ~26600 base T - near line 40-55 (at x1 it would
/// be near line 157-170), so line 100 is already from page 5
TEST_P(ZXPolyGroupModels_Test, PictureFollowsTheBeamAtHostSpeed)
{
    CreateGroup(GetParam());
    EmulatorContext* master = _group->GetContext(0);
    Memory& memory = *master->pMemory;

    const std::vector<uint8_t> code = {
        0x3E, 0x91, 0xED, 0x47, 0xED, 0x5E, 0xFB,          // LD A,#91 ; LD I,A ; IM 2 ; EI
        0x76,                                              // loop: HALT
        0x01, 0xFD, 0x7F, 0x3E, 0x08, 0xED, 0x79,          // OUT (#7FFD),#08 - screen page 7
        0x16, 0x10, 0x06, 0x00, 0x10, 0xFE, 0x15, 0x20, 0xF9, // LD D,16 ; LD B,0 ; DJNZ $ ; DEC D ; JR NZ
        0x01, 0xFD, 0x7F, 0xAF, 0xED, 0x79,                // OUT (#7FFD),#00 - screen page 5
        0x18, 0xE7};                                       // JR loop
    for (size_t i = 0; i < code.size(); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    memory.DirectWriteToZ80Memory(0x91FF, 0x00);
    memory.DirectWriteToZ80Memory(0x9200, 0x82);
    const uint8_t handler[] = {0xFB, 0xED, 0x4D};           // EI ; RETI
    for (size_t i = 0; i < sizeof(handler); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), handler[i]);

    std::memset(memory.RAMPageAddress(5), 0xFF, 6144);     // page 5: all ink
    std::memset(memory.RAMPageAddress(5) + 6144, 0x07, 768);
    std::memset(memory.RAMPageAddress(7), 0x00, 6144);     // page 7: all paper
    std::memset(memory.RAMPageAddress(7) + 6144, 0x07, 768);

    Z80& cpu = *master->pCore->GetZ80();
    cpu.pc = 0x8000;
    cpu.sp = 0x7F00;
    cpu.iff1 = cpu.iff2 = 0;
    _group->ReplicateFromMaster();
    ASSERT_TRUE(_group->GetInstance(0)->SetSpeedMultiplier(2));
    _group->RunFrames(5);
    ASSERT_EQ(master->emulatorState.current_z80_frequency_multiplier, 2u);

    std::vector<uint32_t> picture;
    _group->Compose(picture);
    uint32_t palette[16];
    master->pScreen->GetRGBAPalette16(palette);
    auto at = [&](unsigned line) { return picture[(line * 2) * ZXPolyScreenComposer::OUT_WIDTH + 256]; };
    EXPECT_EQ(at(8), palette[0]) << "line 8 was fetched from page 7 (black)";
    EXPECT_EQ(at(100), palette[7]) << "line 100 was fetched from page 5 (white): the beam spans the stretched frame";
    EXPECT_EQ(at(185), palette[7]) << "line 185 was fetched from page 5 (white)";
}

/// Running the slaves on worker threads gives the same machine as running them
/// one after another: identical per-frame composed pictures and CPU state
TEST_P(ZXPolyGroupModels_Test, ParallelSlavesMatchSequentialSlaves)
{
    auto run = [&](bool parallel, std::vector<uint64_t>& hashes) {
        CreateGroup(GetParam());
        _group->SetParallelSlaves(parallel);
        std::string error;
        ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
            << error;
        std::vector<uint32_t> picture;
        for (unsigned f = 0; f < 150; f++)
        {
            if (f == 40)
                _group->PressKey(ZXKEY_1);
            if (f == 46)
                _group->ReleaseKey(ZXKEY_1);
            _group->RunFrame();
            ASSERT_FALSE(_group->CheckLockstep().diverged);
            _group->Compose(picture);
            uint64_t hash = 1469598103934665603ull;
            for (uint32_t pixel : picture)
                hash = (hash ^ pixel) * 1099511628211ull;
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
                hash = (hash ^ _group->GetContext(m)->pCore->GetZ80()->pc) * 1099511628211ull;
            hashes.push_back(hash);
        }
        _group.reset();
    };

    std::vector<uint64_t> sequential;
    std::vector<uint64_t> parallel;
    run(false, sequential);
    run(true, parallel);
    ASSERT_EQ(sequential.size(), parallel.size());
    for (size_t f = 0; f < sequential.size(); f++)
        ASSERT_EQ(sequential[f], parallel[f]) << "frame " << f;
}

/// Unlimited speed: the slaves' frame overlaps the master's next one. Every
/// module has the same state at every frame boundary as with the slaves
/// finished before the master goes on - including keys queued in the middle
/// of a frame from the master's own thread, as host keys arrive in live mode
/// Boot-bound (1500 frames on four machines, twice)
TEST_P(ZXPolyGroupModels_Test, PipelinedSlavesMatchSynchronousSlaves)
{
    auto run = [&](bool pipelined, std::vector<uint64_t>& hashes, unsigned& overlapped) {
        CreateGroup(GetParam());    // turbo on every member: unlimited speed
        _group->SetPipelinedSlaves(pipelined);
        std::string error;
        ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
            << error;

        EmulatorContext* master = _group->GetContext(0);
        const uint64_t start = master->emulatorState.frame_counter;
        uint64_t lastFrame = ~0ull;
        overlapped = 0;
        master->pCore->GetZ80()->busTraceHook = [&](char, uint16_t, uint8_t) {
            const uint64_t frame = master->emulatorState.frame_counter - start;
            if (frame == lastFrame)
                return;
            lastFrame = frame;
            overlapped += _group->IsPipelining() ? 1u : 0u;
            if (frame == 40)
                _group->PressKey(ZXKEY_1);
            if (frame == 46)
                _group->ReleaseKey(ZXKEY_1);
            if (frame == 70)
                _group->PressKey(ZXKEY_P);
            if (frame == 83)
                _group->ReleaseKey(ZXKEY_P);
        };

        std::vector<uint32_t> picture;
        for (unsigned chunk = 0; chunk < 10; chunk++)
        {
            _group->RunFrames(15);
            ASSERT_FALSE(_group->CheckLockstep().diverged) << "chunk " << chunk;
            _group->Compose(picture);

            uint64_t hash = 1469598103934665603ull;
            auto mix = [&hash](uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
            for (uint32_t pixel : picture)
                mix(pixel);
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            {
                EmulatorContext* context = _group->GetContext(m);
                const Z80& cpu = *context->pCore->GetZ80();
                for (uint64_t v : {uint64_t(cpu.pc), uint64_t(cpu.sp), uint64_t(cpu.af), uint64_t(cpu.bc),
                                   uint64_t(cpu.de), uint64_t(cpu.hl), uint64_t(cpu.ix), uint64_t(cpu.iy),
                                   uint64_t(cpu.t), uint64_t(cpu.r_low), uint64_t(cpu.iff1), uint64_t(cpu.halted),
                                   context->emulatorState.frame_counter})
                    mix(v);
                for (uint16_t page = 0; page < 8; page++)
                {
                    const uint8_t* ram = context->pMemory->RAMPageAddress(page);
                    for (size_t i = 0; i < 16384; i++)
                        mix(ram[i]);
                }
            }
            hashes.push_back(hash);
        }
        master->pCore->GetZ80()->busTraceHook = nullptr;
        _group.reset();
    };

    std::vector<uint64_t> synchronous;
    std::vector<uint64_t> pipelined;
    unsigned overlappedSynchronous = 0;
    unsigned overlappedPipelined = 0;
    run(false, synchronous, overlappedSynchronous);
    run(true, pipelined, overlappedPipelined);

    EXPECT_EQ(overlappedSynchronous, 0u);
    EXPECT_GT(overlappedPipelined, 100u) << "the slaves did not overlap the master";
    ASSERT_EQ(synchronous.size(), pipelined.size());
    for (size_t chunk = 0; chunk < synchronous.size(); chunk++)
        ASSERT_EQ(synchronous[chunk], pipelined[chunk]) << "state after chunk " << chunk;
}

/// Locked, the modules step from frame boundary to frame boundary: the master
/// reading a slave's R0 sees the slave at the last boundary, a slave reading
/// the master's R0 sees the master there - also while the slaves' frame
/// overlaps the master's next one. The program logs every R0 read to memory;
/// all four modules' memory must match between the two schedules
TEST_P(ZXPolyGroupModels_Test, StatusReadsMatchWhilePipelined)
{
    auto run = [&](bool pipelined, std::vector<uint64_t>& hashes, unsigned& overlapped) {
        CreateGroup(GetParam());    // turbo on every member: unlimited speed
        _group->SetPipelinedSlaves(pipelined);
        EmulatorContext* master = _group->GetContext(0);
        Memory& memory = *master->pMemory;
        for (uint16_t page = 0; page < 8; page++)
            std::memset(memory.RAMPageAddress(page), 0x00, 16384);    // power-on RAM is not zero

        // LD HL,#C000
        // loop: LD BC,#3D00 ; IN A,(C) ; AND 3 ; LD B,#00 ; JR NZ,slave ; LD B,#10
        // slave: LD C,#FF ; IN A,(C) ; LD (HL),A ; INC HL ; LD A,H ; OR #C0 ; LD H,A ; JR loop
        // (the master reads module 1's R0 at #10FF, a slave the master's at #00FF)
        const uint8_t code[] = {0x21, 0x00, 0xC0, 0x01, 0x00, 0x3D, 0xED, 0x78, 0xE6, 0x03, 0x06, 0x00, 0x20, 0x02,
                                0x06, 0x10, 0x0E, 0xFF, 0xED, 0x78, 0x77, 0x23, 0x7C, 0xF6, 0xC0, 0x67, 0x18, 0xE7};
        for (size_t i = 0; i < sizeof(code); i++)
            memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
        Z80& cpu = *master->pCore->GetZ80();
        cpu.pc = 0x8000;
        cpu.sp = 0x7F00;
        cpu.iff1 = cpu.iff2 = 0;
        _group->ReplicateFromMaster();

        uint64_t lastFrame = ~0ull;
        overlapped = 0;
        cpu.busTraceHook = [&](char, uint16_t, uint8_t) {
            if (master->emulatorState.frame_counter != lastFrame)
            {
                lastFrame = master->emulatorState.frame_counter;
                overlapped += _group->IsPipelining() ? 1u : 0u;
            }
        };
        for (unsigned chunk = 0; chunk < 5; chunk++)
        {
            _group->RunFrames(10);
            uint64_t hash = 1469598103934665603ull;
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            {
                for (uint16_t page = 0; page < 8; page++)
                {
                    const uint8_t* ram = _group->GetContext(m)->pMemory->RAMPageAddress(page);
                    for (size_t i = 0; i < 16384; i++)
                        hash = (hash ^ ram[i]) * 1099511628211ull;
                }
                hash = (hash ^ _group->GetContext(m)->pCore->GetZ80()->pc) * 1099511628211ull;
            }
            hashes.push_back(hash);
        }
        cpu.busTraceHook = nullptr;
        _group.reset();
    };

    std::vector<uint64_t> synchronous;
    std::vector<uint64_t> pipelined;
    unsigned overlappedSynchronous = 0;
    unsigned overlappedPipelined = 0;
    run(false, synchronous, overlappedSynchronous);
    run(true, pipelined, overlappedPipelined);

    EXPECT_EQ(overlappedSynchronous, 0u);
    EXPECT_GT(overlappedPipelined, 30u) << "the slaves did not overlap the master";
    ASSERT_EQ(synchronous.size(), pipelined.size());
    for (size_t chunk = 0; chunk < synchronous.size(); chunk++)
        ASSERT_EQ(synchronous[chunk], pipelined[chunk]) << "state after chunk " << chunk;
}

/// Lockstep is judged at the frame boundary, where all four stand at the same
/// position: a master already inside its next frame (live mode, where a status
/// request arrives from another thread at any moment) is not a divergence,
/// and a slave that took another branch is one
TEST_P(ZXPolyGroupModels_Test, LockstepIsCheckedAtTheFrameBoundary)
{
    CreateGroup(GetParam());
    EmulatorContext* master = _group->GetContext(0);
    Memory& memory = *master->pMemory;

    // loop: LD A,(#9000) ; OR A ; JR NZ,other ; JR loop
    // other: INC HL ; JR other
    const uint8_t code[] = {0x3A, 0x00, 0x90, 0xB7, 0x20, 0x02, 0x18, 0xF8, 0x23, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(code); i++)
        memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    memory.DirectWriteToZ80Memory(0x9000, 0x00);
    Z80& cpu = *master->pCore->GetZ80();
    cpu.pc = 0x8000;
    cpu.sp = 0x7F00;
    cpu.iff1 = cpu.iff2 = 0;
    _group->ReplicateFromMaster();

    _group->RunFrames(3);
    ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    EXPECT_FALSE(divergence.diverged) << divergence.what;

    // The master alone goes 2000 T into its next frame
    _group->GetInstance(0)->RunUntilCondition([](const Z80State& state) { return state.t >= 2000; }, 0, false);
    divergence = _group->CheckLockstep();
    EXPECT_FALSE(divergence.diverged) << "a master inside its next frame was taken for a divergence: "
                                      << divergence.what;

    // Module 2 finds another value and branches away
    _group->GetContext(2)->pMemory->DirectWriteToZ80Memory(0x9000, 0x01);
    _group->RunFrames(1);
    divergence = _group->CheckLockstep();
    EXPECT_TRUE(divergence.diverged) << "a slave on another branch was not detected";
    EXPECT_EQ(divergence.module, 2u);
}

/// The host speed control (x2..x16) stretches the master's frame to N x the
/// T-states. The slaves take the same multiplier at the same frame start, so
/// the machine stays in lockstep through speed changes - in both schedules,
/// with identical states
/// Boot-bound (40 frames stretched up to x8 on four machines, twice)
TEST_P(ZXPolyGroupModels_Test, SpeedMultiplierKeepsTheSlavesInStep)
{
    auto run = [&](bool pipelined, std::vector<uint64_t>& hashes) {
        CreateGroup(GetParam());    // turbo on every member: unlimited speed
        _group->SetPipelinedSlaves(pipelined);
        std::string error;
        ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
            << error;
        Emulator* master = _group->GetInstance(0);
        for (uint8_t speed : {uint8_t(2), uint8_t(4), uint8_t(1), uint8_t(8)})
        {
            ASSERT_TRUE(master->SetSpeedMultiplier(speed));
            for (unsigned chunk = 0; chunk < 2; chunk++)
            {
                _group->RunFrames(5);
                const ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
                ASSERT_FALSE(divergence.diverged) << "x" << int(speed) << " chunk " << chunk << ": module "
                                                  << divergence.module << " " << divergence.what;
                for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
                    ASSERT_EQ(_group->GetContext(m)->emulatorState.current_z80_frequency_multiplier, speed)
                        << "module " << m;

                uint64_t hash = 1469598103934665603ull;
                for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
                {
                    const Z80& cpu = *_group->GetContext(m)->pCore->GetZ80();
                    for (uint64_t v : {uint64_t(cpu.pc), uint64_t(cpu.sp), uint64_t(cpu.af), uint64_t(cpu.t)})
                        hash = (hash ^ v) * 1099511628211ull;
                    for (uint16_t page = 0; page < 8; page++)
                    {
                        const uint8_t* ram = _group->GetContext(m)->pMemory->RAMPageAddress(page);
                        for (size_t i = 0; i < 16384; i += 3)
                            hash = (hash ^ ram[i]) * 1099511628211ull;
                    }
                }
                hashes.push_back(hash);
            }
        }
        _group.reset();
    };

    std::vector<uint64_t> synchronous;
    std::vector<uint64_t> pipelined;
    run(false, synchronous);
    run(true, pipelined);
    ASSERT_EQ(synchronous.size(), pipelined.size());
    for (size_t chunk = 0; chunk < synchronous.size(); chunk++)
        ASSERT_EQ(synchronous[chunk], pipelined[chunk]) << "state after chunk " << chunk;
}

/// Live mode: the master runs its own frame loop and the group works in its
/// frame-end hook, before the master's next frame starts. A speed change
/// requested from another thread while that hook runs (here: from a slave's
/// worker thread, the widest window) must reach all four at the same frame
/// start - the master may not take it one frame before the slaves
/// Boot-bound (60 stretched frames on four machines)
TEST_P(ZXPolyGroupModels_Test, SpeedChangeDuringTheFrameHookReachesAllFourTogether)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
        << error;
    _group->AttachToMaster();
    MainLoopCUT* loop = reinterpret_cast<MainLoopCUT*>(_group->GetContext(0)->pMainLoop);
    Emulator* master = _group->GetInstance(0);

    // On slave 1's first bus access of every 5th frame, request the next speed
    const uint8_t speeds[] = {2, 4, 1, 8, 2, 1};
    size_t next = 0;
    EmulatorContext* slave = _group->GetContext(1);
    uint64_t lastFrame = ~0ull;
    slave->pCore->GetZ80()->busTraceHook = [&](char, uint16_t, uint8_t) {
        const uint64_t frame = slave->emulatorState.frame_counter;
        if (frame == lastFrame)
            return;
        lastFrame = frame;
        if (frame % 5 == 0)
            master->SetSpeedMultiplier(speeds[next++ % sizeof(speeds)]);
    };

    for (unsigned f = 0; f < 60; f++)
    {
        loop->RunFramePublic();
        const ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
        ASSERT_FALSE(divergence.diverged) << "frame " << f << ": module " << divergence.module << " "
                                          << divergence.what;
        for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
            ASSERT_EQ(_group->GetContext(m)->emulatorState.current_z80_frequency_multiplier,
                      _group->GetContext(0)->emulatorState.current_z80_frequency_multiplier)
                << "frame " << f << " module " << m;
    }
    EXPECT_GE(next, 8u) << "the speed changes were not requested";
    slave->pCore->GetZ80()->busTraceHook = nullptr;
    _group->DetachFromMaster();
}

/// Time travel of one member would split it from the others, so a ZX-Poly
/// machine refuses it on every member, with the reason every surface shows:
/// recording (the debugger's live history starts through it) and loading a
/// session. The group's own timeline (core only) lifts it while it records
TEST_F(ZXPolyGroup_Test, TimeTravelIsRefusedOnEveryMember)
{
    CreateGroup("PENTAGON");
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
    {
        ttd::TimeTravelManager* ttd = _group->GetContext(m)->pTimeTravelManager;
        ASSERT_NE(ttd, nullptr);
        EXPECT_FALSE(ttd->StartRecording()) << "module " << m;
        EXPECT_FALSE(ttd->IsRecording()) << "module " << m;
        const std::string reason = ttd->GetSessionInfo().unavailableReason;
        EXPECT_NE(reason.find("ZX-Poly"), std::string::npos) << "module " << m << ": '" << reason << "'";

        std::istringstream file("UTTD");
        std::string error;
        EXPECT_FALSE(ttd->DeserializeSession(file, error)) << "module " << m;
        EXPECT_EQ(error, reason) << "module " << m;
    }

    // The group timeline records all four; afterwards the refusal is back
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error)) << error;
    ASSERT_TRUE(_group->StartRecording(&error)) << error;
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        EXPECT_TRUE(_group->GetContext(m)->pTimeTravelManager->IsRecording()) << "module " << m;
    _group->RunFrames(2);
    _group->StopRecording();
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
    {
        ttd::TimeTravelManager* ttd = _group->GetContext(m)->pTimeTravelManager;
        EXPECT_FALSE(ttd->IsRecording()) << "module " << m;
        EXPECT_FALSE(ttd->StartRecording()) << "module " << m;
    }
}

/// Automation input (WebAPI, MCP, CLI, Lua, Python) reaches the machine through
/// the master's DebugKeyboardManager / DebugMouseManager and the TTD live-input
/// gateway. Given to the master alone it would split the machine; the group
/// takes it and gives it to all four at one frame boundary. The keys give the
/// same machine as the group's own queue, and the mouse lands on every member
/// Boot-bound (150 frames on four machines, twice)
TEST_P(ZXPolyGroupModels_Test, AutomationInputReachesAllFourModules)
{
    auto run = [&](bool throughAutomation, std::vector<uint64_t>& hashes) {
        CreateGroup(GetParam());
        std::string error;
        ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
            << error;
        DebugKeyboardManager* keys = _group->GetContext(0)->pDebugManager->GetKeyboardManager();
        for (unsigned f = 0; f < 150; f++)
        {
            if (f == 40)
                throughAutomation ? keys->PressKey(ZXKEY_1) : _group->PressKey(ZXKEY_1);
            if (f == 46)
                throughAutomation ? keys->ReleaseKey(ZXKEY_1) : _group->ReleaseKey(ZXKEY_1);
            _group->RunFrame();
            const ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
            ASSERT_FALSE(divergence.diverged) << "frame " << f << ": module " << divergence.module << " "
                                              << divergence.what;
            uint64_t hash = 1469598103934665603ull;
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            {
                const Z80& cpu = *_group->GetContext(m)->pCore->GetZ80();
                for (uint64_t v : {uint64_t(cpu.pc), uint64_t(cpu.sp), uint64_t(cpu.t)})
                    hash = (hash ^ v) * 1099511628211ull;
            }
            hashes.push_back(hash);
        }

        // The Kempston mouse through the automation path: every member moves
        DebugMouseManager* mouse = _group->GetContext(0)->pDebugManager->GetMouseManager();
        mouse->SetCounters(10, 20);
        mouse->Move(5, -3);
        _group->RunFrame();
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        {
            const Mouse* device = _group->GetContext(m)->pMouse;
            ASSERT_NE(device, nullptr);
            EXPECT_EQ(device->GetX(), _group->GetContext(0)->pMouse->GetX()) << "module " << m;
            EXPECT_EQ(device->GetY(), _group->GetContext(0)->pMouse->GetY()) << "module " << m;
        }
        EXPECT_NE(_group->GetContext(1)->pMouse->GetX(), 10u) << "the move did not reach the slaves";
        _group.reset();
    };

    std::vector<uint64_t> group;
    std::vector<uint64_t> automation;
    run(false, group);
    run(true, automation);
    ASSERT_EQ(group.size(), automation.size());
    for (size_t f = 0; f < group.size(); f++)
        ASSERT_EQ(group[f], automation[f]) << "frame " << f;
}

/// The manager is the one source every surface (WebAPI, MCP, CLI, Lua,
/// Python, Qt) creates ZX-Poly machines from: a group of four registered by
/// its master, described in MachineIdentity, removed as a whole
TEST_F(ZXPolyGroup_Test, ManagerCreatesDescribesAndRemovesAGroup)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> master = manager->CreateZXPolyMachine(
        "zxpoly-managed", "PENTAGON", TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error);
    ASSERT_TRUE(master) << error;

    ZXPolyGroup* group = manager->GetZXPolyGroup(master->GetId());
    ASSERT_NE(group, nullptr);
    const ZXPolyGroup::Status status = group->GetStatus();
    EXPECT_EQ(status.memberIds[0], master->GetId());
    EXPECT_TRUE(status.locked);
    EXPECT_EQ(status.videoMode, 4);

    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*master);
    EXPECT_TRUE(identity.ZXPoly);
    EXPECT_EQ(identity.ZXPolyModule, 0);
    EXPECT_EQ(identity.ZXPolyMasterId, master->GetId());
    EXPECT_TRUE(identity.ZXPolyLocked);
    EXPECT_EQ(identity.ZXPolyVideoMode, 4);

    std::shared_ptr<Emulator> slave = manager->GetEmulator(status.memberIds[2]);
    ASSERT_TRUE(slave);
    EXPECT_EQ(EmulatorManager::GetMachineIdentity(*slave).ZXPolyModule, 2);
    EXPECT_EQ(manager->GetZXPolyGroup(status.memberIds[2]), group);
    slave.reset();

    const std::string masterId = master->GetId();
    master.reset();
    EXPECT_TRUE(manager->RemoveEmulator(masterId));
    EXPECT_EQ(manager->GetZXPolyGroup(masterId), nullptr);
    for (const std::string& id : status.memberIds)
        EXPECT_FALSE(manager->GetEmulator(id)) << id << " outlived its group";

    EXPECT_FALSE(manager->CreateZXPolyMachine("", "NOT_A_MODEL", "", &error));
    EXPECT_FALSE(error.empty());
}

/// The named configurations resolve case-insensitively to their base models,
/// and creating one by name through the ordinary create-by-model path (every
/// automation surface ends there) builds the whole group
TEST_F(ZXPolyGroup_Test, ConfigurationsCreateTheGroupByName)
{
    ASSERT_EQ(ZXPolyGroup::Configurations().size(), 3u);
    const std::vector<std::pair<std::string, MEM_MODEL>> expected = {
        {"ZXPOLY-48K", MM_SPECTRUM48}, {"zxpoly-128k", MM_SPECTRUM128}, {"ZXPoly-Pentagon", MM_PENTAGON}};
    EXPECT_EQ(ZXPolyGroup::FindConfiguration("PENTAGON"), nullptr);

    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (const auto& [name, model] : expected)
    {
        ASSERT_NE(ZXPolyGroup::FindConfiguration(name), nullptr) << name;

        std::string error;
        std::shared_ptr<Emulator> master = manager->CreateEmulatorWithModel("", name, LoggerLevel::LogError, &error);
        ASSERT_TRUE(master) << name << ": " << error;
        ZXPolyGroup* group = manager->GetZXPolyGroup(master->GetId());
        ASSERT_NE(group, nullptr) << name;
        EXPECT_EQ(group->GetMaster(), master) << name;
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            EXPECT_EQ(group->GetContext(m)->config.mem_model, model) << name << " module " << m;
        EXPECT_TRUE(EmulatorManager::GetMachineIdentity(*master).ZXPoly) << name;

        const std::string masterId = master->GetId();
        master.reset();
        EXPECT_TRUE(manager->RemoveEmulator(masterId)) << name;
    }

    // A configuration fixes its RAM: an explicit size is refused, not ignored
    std::string error;
    EXPECT_FALSE(manager->CreateEmulatorWithModelAndRAM("", "ZXPOLY-128K", 256, LoggerLevel::LogError, &error));
    EXPECT_NE(error.find("ZX-Poly"), std::string::npos) << error;
}

/// A model switch (ModelSwitch: every surface's switch path) into a
/// configuration builds the group; switching away from its master removes
/// the whole group, hidden slaves included
TEST_F(ZXPolyGroup_Test, ModelSwitchEntersAndLeavesAConfiguration)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> plain = manager->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError, &error);
    ASSERT_TRUE(plain) << error;

    ModelSwitchRequest request;
    request.emulatorId = plain->GetId();
    request.model = "ZXPOLY-128K";
    plain.reset();
    ModelSwitchResult switched = ModelSwitch::Run(request);
    ASSERT_TRUE(switched.result.Ok()) << switched.result.message;
    ZXPolyGroup* group = manager->GetZXPolyGroup(switched.emulator->GetId());
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->GetMaster(), switched.emulator);
    EXPECT_EQ(group->GetContext(1)->config.mem_model, MM_SPECTRUM128);
    const ZXPolyGroup::Status status = group->GetStatus();

    request.emulatorId = switched.emulator->GetId();
    request.model = "48K";
    switched.emulator.reset();
    switched = ModelSwitch::Run(request);
    ASSERT_TRUE(switched.result.Ok()) << switched.result.message;
    EXPECT_EQ(manager->GetZXPolyGroup(switched.emulator->GetId()), nullptr);
    for (const std::string& id : status.memberIds)
        EXPECT_FALSE(manager->GetEmulator(id)) << id << " outlived its group";

    const std::string id = switched.emulator->GetId();
    switched.emulator.reset();
    EXPECT_TRUE(manager->RemoveEmulator(id));
}

/// A 48K group runs the synchronized quad (replicated 48K software), but the
/// ZX-Poly editions page through #7FFD: .zxp and the ZX-Poly ROM are refused
/// with the reason instead of running into a diverged machine
TEST_F(ZXPolyGroup_Test, FortyEightKGroupRunsReplicatedSoftwareButRefusesEditions)
{
    CreateGroup("ZXPOLY-48K");
    ASSERT_EQ(_group->GetContext(0)->config.mem_model, MM_SPECTRUM48);

    std::string error;
    EXPECT_FALSE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error));
    EXPECT_NE(error.find("128K"), std::string::npos) << error;
    error.clear();
    EXPECT_FALSE(_group->LoadPROM(TestPathHelper::GetTestDataPath("machines/zxpoly/rom/zxpolytest.prom"), &error));
    EXPECT_NE(error.find("128K"), std::string::npos) << error;
    EXPECT_FALSE(_group->IsLocked());

    // Replicated 48K state stays one machine
    _group->RunFrame();
    _group->ReplicateFromMaster();
    ZXPolyGroup::Divergence divergence;
    EXPECT_EQ(RunInLockstep(50, divergence), 50u) << divergence.what;
}

/// Group TTD: four sessions started together, group input journaled in each,
/// the platform state kept per frame. A seek back restores all four modules to
/// the recorded frame; continuing with the same input reproduces the recorded
/// frames exactly; a new branch with different input stays in lockstep.
/// Boot-bound (hundreds of frames on four machines, recorded)
TEST_P(ZXPolyGroupModels_Test, GroupTimeTravelSeeksAndReplays)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/SummerSanta2022.zxp"), &error))
        << error;
    ASSERT_TRUE(_group->StartRecording(&error)) << error;

    auto hashState = [&]() {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        {
            const Z80& cpu = *_group->GetContext(m)->pCore->GetZ80();
            mix(cpu.pc);
            mix(cpu.sp);
            mix(cpu.af);
            mix(cpu.t);
            for (uint16_t page : {uint16_t(2), uint16_t(5)})
            {
                const uint8_t* ram = _group->GetContext(m)->pMemory->RAMPageAddress(page);
                for (size_t i = 0; i < 16384; i += 7)
                    mix(ram[i]);
            }
        }
        mix(_group->GetPort3D00());
        return hash;
    };
    auto script = [&](unsigned frame) {    // the input of frame `frame`
        if (frame == 40)
            _group->PressKey(ZXKEY_1);
        if (frame == 46)
            _group->ReleaseKey(ZXKEY_1);
        if (frame == 70)
            _group->PressKey(ZXKEY_P);
        if (frame == 80)
            _group->ReleaseKey(ZXKEY_P);
    };

    std::map<uint64_t, uint64_t> recorded;
    recorded[_group->GetRecordedPosition()] = hashState();
    for (unsigned f = 0; f < 120; f++)
    {
        script(f);
        _group->RunFrame();
        ASSERT_FALSE(_group->CheckLockstep().diverged) << "frame " << f;
        recorded[_group->GetRecordedPosition()] = hashState();
    }

    // Seek back: every module at the recorded frame
    ASSERT_TRUE(_group->SeekToFrame(60, &error)) << error;
    EXPECT_EQ(_group->GetRecordedPosition(), 60u);
    EXPECT_EQ(hashState(), recorded[60]) << "the group did not come back to frame 60";

    // Continue with the same input: the recorded frames again
    ASSERT_TRUE(_group->ResumeRecording(&error)) << error;
    for (unsigned f = 60; f < 90; f++)
    {
        script(f);
        _group->RunFrame();
        ASSERT_FALSE(_group->CheckLockstep().diverged) << "frame " << f;
        ASSERT_EQ(hashState(), recorded[_group->GetRecordedPosition()]) << "replayed frame " << f + 1;
    }

    // A new branch with other input: still one machine
    ASSERT_TRUE(_group->SeekToFrame(65, &error)) << error;
    ASSERT_TRUE(_group->ResumeRecording(&error)) << error;
    for (unsigned f = 65; f < 110; f++)
    {
        if (f == 66)
            _group->PressKey(ZXKEY_O);
        if (f == 90)
            _group->ReleaseKey(ZXKEY_O);
        _group->RunFrame();
        ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
        ASSERT_FALSE(divergence.diverged) << "branch frame " << f << ": " << divergence.what;
    }
    _group->StopRecording();
}
