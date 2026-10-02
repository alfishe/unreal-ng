// SprinterInput: the AT keyboard on the Z84C15 SIO channel A, the keyboard INT,
// the PLD's keyboard actions and the serial mouse on SIO channel B (Sprinter
// tdd-accel-sound-input §3, test-plan §2.10 "keyboard encoder").
//
// The machine tests drive the keys where the journaled host input lands
// (Keyboard::ApplyPcKey, the TTD input apply) and move emulated time with the
// cumulative T-state counter; the boot tests press keys through the automation
// keyboard (DebugKeyboardManager, the type_input / key injection surfaces)
// against the real BIOS 3.04.

#include "sprinterfixture.h"

#include <cstdio>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/media/mediamanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/models/sprinter/sprinterinput.h"
#include "emulator/state/devicestate.h"

class SprinterInput_Test : public SprinterFixture
{
protected:
    static constexpr uint32_t kByteT = 3210;  ///< one PS/2 frame at 3.5 MHz

    SprinterInput& Input() { return _decoder->GetInput(); }
    Z84Lib::Z84Sio& Sio() { return _decoder->GetZ84().sio; }

    void Key(PcKey key, bool pressed) { _context->pKeyboard->ApplyPcKey(key, pressed); }
    void Wait(uint64_t tStates) { _context->emulatorState.t_states += tStates; }

    /// SIO RR0 / RR1 as the BIOS reads them (#19: WR0 pointer, then the register)
    uint8_t Rr0() { return In(0x0019); }
    uint8_t Rr1()
    {
        Out(0x0019, 0x01);
        return In(0x0019);
    }

    /// Everything SIO A holds now, through the ports
    std::vector<uint8_t> DrainA()
    {
        std::vector<uint8_t> out;
        while (Rr0() & 0x01)
            out.push_back(In(0x0018));
        return out;
    }

    /// ALL_MODE through its port (#204E, code #C3) after opening the decoder
    void WriteAllMode(uint8_t value)
    {
        SetCodeAll(0x204E, false, 0xC3);
        OpenDcp();
        Out(0x204E, value);
    }
};

// The Sprinter's decoder is the host keyboard's PS/2 sink: host keys reach both the matrix and SIO A
TEST_F(SprinterInput_Test, AttachesAsThePs2Sink)
{
    ASSERT_NE(_context->pKeyboard, nullptr);
    EXPECT_EQ(_context->pKeyboard->GetPs2Sink(), &Input());
    EXPECT_TRUE(_context->pKeyboard->RoutesToPs2());
    EXPECT_TRUE(_context->pKeyboard->RoutesToMatrix()) << "the PLD's ZX matrix (#FE) hears the same keyboard";
}

// F4 (set 2 #0C), as the BIOS's IDE wait asks: one PS/2 frame after the press it is in SIO A
TEST_F(SprinterInput_Test, KeyReachesSioAAfterOneFrame)
{
    DrainA();
    Key(PcKey::Function4, true);
    Wait(kByteT - 1);
    EXPECT_EQ(Rr0() & 0x01, 0) << "the frame has not ended";
    Wait(1);
    EXPECT_EQ(Rr0() & 0x01, 1) << "RR0 bit 0: a character is waiting";
    EXPECT_EQ(In(0x0018), 0x0C);
    EXPECT_EQ(Rr0() & 0x01, 0);

    Key(PcKey::Function4, false);
    Wait(3 * kByteT);
    EXPECT_EQ(DrainA(), (std::vector<uint8_t>{0xF0, 0x0C}));
}

// Nothing holds the keyboard off: a fourth byte into the 3-byte FIFO is an overrun (RR1 bit 5),
// which SETUP's KEYSCAN checks (KEY.ASM Receiver_Overrun)
TEST_F(SprinterInput_Test, FullFifoOverruns)
{
    DrainA();
    Key(PcKey::Delete, true);   // E0 71
    Key(PcKey::Delete, false);  // E0 F0 71
    Wait(5 * kByteT);
    EXPECT_NE(Rr1() & 0x20, 0) << "overrun";
    EXPECT_EQ(DrainA(), (std::vector<uint8_t>{0xE0, 0x71, 0xE0}));
    EXPECT_EQ(Input().KeyboardOverruns(), 2u);
}

// Read in time (one frame INT per byte or faster), every byte arrives
TEST_F(SprinterInput_Test, PolledInTimeNoOverrun)
{
    DrainA();
    Key(PcKey::Delete, true);
    Key(PcKey::Delete, false);
    std::vector<uint8_t> got;
    for (int i = 0; i < 5; i++)
    {
        Wait(kByteT);
        const std::vector<uint8_t> part = DrainA();
        got.insert(got.end(), part.begin(), part.end());
    }
    EXPECT_EQ(got, (std::vector<uint8_t>{0xE0, 0x71, 0xE0, 0xF0, 0x71}));
    EXPECT_EQ(Rr1() & 0x20, 0);
}

// ALL_MODE bits 0 and 3: each byte raises the PLD INT (vector #FF) on time, through the step hook
// that runs only while a byte is on its way; off, no INT and no per-step cost
TEST_F(SprinterInput_Test, KeyboardIntFollowsAllMode)
{
    SprinterIntSource& ints = _decoder->GetIntSource();

    WriteAllMode(0x01);  // accelerator / keyboard on, bit 3 off
    Key(PcKey::A, true);
    EXPECT_FALSE(Input().NeedsStepHook());
    EXPECT_NE(_z80->GetMachineStepHook(), static_cast<IMachineStepHook*>(_decoder));
    Wait(kByteT);
    _decoder->OnMachineStep(0);
    EXPECT_FALSE(ints.KeyboardIntLatched());
    DrainA();

    WriteAllMode(0x09);
    Key(PcKey::A, false);  // F0 1C
    EXPECT_TRUE(Input().NeedsStepHook());
    EXPECT_EQ(_z80->GetMachineStepHook(), static_cast<IMachineStepHook*>(_decoder));
    Wait(kByteT - 1);
    _decoder->OnMachineStep(0);
    EXPECT_FALSE(ints.KeyboardIntLatched()) << "the frame has not ended";
    Wait(1);
    _decoder->OnMachineStep(0);
    EXPECT_TRUE(ints.KeyboardIntLatched());
    EXPECT_TRUE(ints.IsIntAsserted(_z80->t));
    EXPECT_EQ(ints.AcknowledgeInterrupt(_z80->t), 0xFF);
    EXPECT_FALSE(ints.KeyboardIntLatched()) << "the acknowledge clears the PLD's INT";

    Wait(kByteT);
    _decoder->OnMachineStep(0);
    EXPECT_TRUE(ints.KeyboardIntLatched()) << "one INT per byte";
    EXPECT_FALSE(Input().NeedsStepHook());
    EXPECT_NE(_z80->GetMachineStepHook(), static_cast<IMachineStepHook*>(_decoder)) << "idle again: the hook is gone";
    EXPECT_EQ(DrainA(), (std::vector<uint8_t>{0xF0, 0x1C}));
}

// The Spectrum-mode view: the same host key reaches the ZX matrix at #FE (code #40) as well
TEST_F(SprinterInput_Test, HostKeyAlsoReachesTheZxMatrix)
{
    SetCodeAll(0xFDFE, true, SprinterCode::Keyboard);
    OpenDcp();
    EXPECT_EQ(In(0xFDFE) & 0x01, 0x01);
    _context->pKeyboard->PressKey(ZXKEY_A);  // the KeyboardEvent half of the host key
    Key(PcKey::A, true);                     // the PcKeyEvent half
    EXPECT_EQ(In(0xFDFE) & 0x01, 0x00) << "row A..G, bit 0 = A";
    Wait(kByteT);
    EXPECT_EQ(DrainA(), (std::vector<uint8_t>{0x1C}));
}

// The PLD's keyboard block: Ctrl + Alt + Del pulls the CPU's /RESET (the configuration stays)
TEST_F(SprinterInput_Test, CtrlAltDelResetsTheCpu)
{
    Key(PcKey::LeftCtrl, true);
    Key(PcKey::Delete, true);
    EXPECT_EQ(Pld().resetPending, 0) << "Ctrl + Del alone is a key";
    Key(PcKey::Delete, false);
    Key(PcKey::LeftAlt, true);
    Key(PcKey::Delete, true);
    EXPECT_EQ(Pld().resetPending, static_cast<uint8_t>(static_cast<uint8_t>(SprinterResetKind::SoftReset) + 1));
}

// F12 alone flips the turbo switch (KB_F12 -> TURBO_HAND); with Shift it is a key
TEST_F(SprinterInput_Test, F12TogglesTheTurboSwitch)
{
    const uint8_t before = Pld().turboHard;
    Key(PcKey::Function12, true);
    EXPECT_EQ(Pld().turboHard, before ^ 1);
    Key(PcKey::Function12, false);
    Key(PcKey::LeftShift, true);
    Key(PcKey::Function12, true);
    EXPECT_EQ(Pld().turboHard, before ^ 1) << "Shift + F12 does not switch";
}

// The serial mouse: a move of the host mouse arrives at SIO B as a Microsoft packet
TEST_F(SprinterInput_Test, MouseMoveReachesSioB)
{
    ASSERT_NE(_context->pMouse, nullptr);
    while (In(0x001B) & 0x01)
        In(0x001A);
    _context->pMouse->Move(5, 3);  // 5 right, 3 up
    EXPECT_EQ(In(0x001B) & 0x01, 0) << "the poll starts the packet";
    Wait(3 * 26250);
    std::vector<uint8_t> got;
    while (In(0x001B) & 0x01)
        got.push_back(In(0x001A));
    EXPECT_EQ(got, (std::vector<uint8_t>{0x4C, 0x05, 0x3D}));
}

/// region <BIOS 3.04 with the host keyboard>

// The real BIOS, keys through the automation keyboard (DebugKeyboardManager: what type_input and
// the WebAPI / MCP key surfaces call), which hands the PC keys to the journaled input path
class SprinterInputBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    DebugKeyboardManager* _keys = nullptr;

    void SetUp() override
    {
        if (!SprinterFixture::Rom304Available())
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-input", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);
        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
        _emulator->EnableTurboMode();  // no assertion looks at pixels
        _keys = _emulator->GetDebugManager()->GetKeyboardManager();
        ASSERT_NE(_keys, nullptr);
    }

    void TearDown() override
    {
        _emulator.reset();
        if (!_blankDisk.empty())
            std::remove(_blankDisk.c_str());
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// The text screen from the video RAM mode table (SprinterBoot_Test::ScreenText)
    std::string ScreenText()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
        {
            for (uint8_t b = 0; b < 32; b++)
            {
                for (uint8_t a = 0; a < 40; a++)
                {
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint32_t line = (1u + 2u * a + half + 0x80u * page) * 1024u;
                        const uint8_t c = vram.Read(line + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                }
                text.push_back('\n');
            }
        }
        return text;
    }
    bool ScreenHas(const std::string& needle) { return ScreenText().find(needle) != std::string::npos; }

    bool RunUntilScreen(const std::string& needle, int maxFrames)
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(needle); }, maxFrames, 5);
        return ScreenHas(needle);
    }

    /// Past the IDE detection: with no drive the empty channel reads #7F (DD7 pull-down) and both units are
    /// "None" at once
    void SkipIdeWaits()
    {
        ASSERT_TRUE(RunUntilScreen("Primary Slave    ... None", 400)) << ScreenText();
        ASSERT_TRUE(ScreenHas("Primary Master   ... None")) << ScreenText();
    }

    /// F4 at the one IDE wait BIOS 3.04 still has: a blank disk on the master, so the absent slave gets status #00
    /// from it and the NOP check waits #118 HALTs (~5.7 s) for DRDY, offering F4
    void SkipTheSlaveWaitWithF4()
    {
        std::vector<uint8_t> disk(1024 * 1024, 0);
        _blankDisk = TestPathHelper::GetUniqueTestScratchPath("input-blank-hdd.img");
        ASSERT_TRUE(FileHelper::SaveBufferToFile(_blankDisk, disk.data(), disk.size()));
        MediaSource source;
        source.path = _blankDisk;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;
        const auto result = _context->pMediaManager->Insert("ide0.master", source, options);
        ASSERT_TRUE(result.Ok()) << result.message;

        const char* waiting = "Primary Slave    ... [Press F4";
        ASSERT_TRUE(RunUntilScreen(waiting, 400)) << ScreenText();
        EXPECT_TRUE(ScreenHas("Primary Master   ... UNREAL-NG HDD")) << ScreenText();
        _keys->TapKey("f4");
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(waiting); }, 100, 1);
        ASSERT_FALSE(ScreenHas(waiting)) << "F4 did not skip: " << ScreenText();
        EXPECT_FALSE(ScreenHas("Primary Slave    ... None")) << "F4, not the timeout, ended the wait: " << ScreenText();
    }
    std::string _blankDisk;
};

// The boot prompt: F4 skips the IDE wait (the absent slave next to a blank master disk), ENTER reboots (SETUP
// starts over: the boot screen again), DEL on the boot screen opens SETUP.
// Boot-bound (the logo, the IDE detection, the failed boot, and SETUP's restart: ~900 frames of real ROM)
TEST_F(SprinterInputBoot_Test, Bios304_F4EnterDel)
{
    SkipTheSlaveWaitWithF4();
    const char* prompt = "PRESS <ENTER> TO REBOOT, <ESC> TO CANCEL";
    ASSERT_TRUE(RunUntilScreen(prompt, 1000)) << ScreenText();

    _keys->TapKey("enter");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(prompt); }, 300, 1);
    ASSERT_FALSE(ScreenHas(prompt)) << "ENTER did not reboot: " << ScreenText();
    ASSERT_TRUE(RunUntilScreen("Sprinter BIOS: ver 3.04", 300)) << ScreenText();

    _keys->TapKey("pc.delete");
    ASSERT_TRUE(RunUntilScreen("SPRINTER SETUP UTILITY", 400)) << ScreenText();
    EXPECT_EQ(_decoder->GetInput().KeyboardOverruns(), 0u);
}

// ESC at the prompt leaves SETUP for the Spectrum mode (DSETUP.ASM AGAKEY: ESC -> EXSETUP). With no
// Spectrum ROM installed (DSS SPECTRUM.EXE loads one) the BIOS says so and waits for Ctrl+Alt+Del, which
// the PLD's keyboard block turns into a CPU reset: the BIOS starts over.
// Boot-bound (the logo, the IDE detection, the failed boot, the restart: ~500 frames of real ROM)
TEST_F(SprinterInputBoot_Test, Bios304_EscThenCtrlAltDel)
{
    SkipIdeWaits();
    const char* prompt = "PRESS <ENTER> TO REBOOT, <ESC> TO CANCEL";
    ASSERT_TRUE(RunUntilScreen(prompt, 1000)) << ScreenText();
    EXPECT_EQ(_decoder->GetPldState().allMode, 0xFF) << "SETUP runs with the keyboard INT on (ALL_MODE bits 0, 3)";

    _keys->TapKey("pc.esc");
    const char* noRom = "Spectrum ROM not installed.  Use spectrum.exe  Press Ctrl+Alt+Del or RESET";
    ASSERT_TRUE(RunUntilScreen(noRom, 100)) << ScreenText();
    EXPECT_FALSE(ScreenHas(prompt));

    const std::vector<std::string> ctrlAltDel = {"lctrl", "lalt", "pc.delete"};
    _keys->TapCombo(ctrlAltDel);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(noRom); }, 100, 1);
    ASSERT_FALSE(ScreenHas(noRom)) << "Ctrl+Alt+Del did not reset";
    EXPECT_TRUE(RunUntilScreen("Sprinter BIOS: ver 3.04", 300)) << ScreenText();
}

// type_input on DSS (automation-outcome.md; S4 open point "DSS typing"): text typed through the
// automation keyboard (DebugKeyboardManager::TypeText, behind WebAPI /keyboard/type and MCP type_input)
// reaches the DSS shell as PC keys. The DSS 1.62 floppy boots from drive B with its SYSTEM.BAT ending
// before "fn" (Flex Navigator does not run yet); "dir" lists the floppy, read back with the automation
// screen text (DeviceState::SprinterText, /state/sprinter/text) as well as the test's own reader.
// Boot-bound (BIOS POST, the IDE waits, DSS from the floppy at 21 MHz, the typing): ~3 s host time
TEST_F(SprinterInputBoot_Test, Dss162_TypeInputReachesTheShell)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";

    // SYSTEM.BAT is the third root entry (LBA 19), cluster 49 = LBA 80; drop its last line "fn"
    std::vector<uint8_t> disk(1474560);
    {
        FILE* f = std::fopen(image.c_str(), "rb");
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(std::fread(disk.data(), 1, disk.size(), f), disk.size());
        std::fclose(f);
    }
    uint8_t* entry = disk.data() + 19 * 512 + 2 * 32;
    ASSERT_EQ(std::string(reinterpret_cast<const char*>(entry), 11), "SYSTEM  BAT");
    const uint32_t size = entry[28] | entry[29] << 8;
    const std::string bat(reinterpret_cast<const char*>(disk.data() + 80 * 512), size);
    ASSERT_EQ(bat.substr(bat.size() - 4), "fn\r\n");
    entry[28] = static_cast<uint8_t>(size - 4);
    const std::string copy = TestPathHelper::GetUniqueTestScratchPath("dss162-nofn.img");
    ASSERT_TRUE(FileHelper::SaveBufferToFile(copy, disk.data(), disk.size()));
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(copy, 1, &error)) << error;

    SkipIdeWaits();
    ASSERT_TRUE(RunUntilScreen("Estex DSS Version 1.62.92", 3000)) << ScreenText();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 10);  // the prompt after "ver"

    _keys->TypeText("dir\n");
    ASSERT_TRUE(RunUntilScreen("SYSTEM   EXE", 600)) << ScreenText();
    EXPECT_TRUE(ScreenHas("B:\\>dir")) << ScreenText();

    // The automation's screen text sees the same listing
    const StateNode text = DeviceState::SprinterText(_context);
    std::string all;
    for (const StateNode& line : text.find("lines")->items)
        all += line.find("text")->s + "\n";
    EXPECT_NE(all.find("Directory of B:\\"), std::string::npos) << all;
    EXPECT_NE(all.find("SYSTEM   DOS"), std::string::npos) << all;
    EXPECT_EQ(_decoder->GetInput().KeyboardOverruns(), 0u);
}

/// endregion </BIOS 3.04 with the host keyboard>
