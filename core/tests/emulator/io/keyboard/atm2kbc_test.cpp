// ATM Turbo 2+ keyboard controller: the real firmware images on the MCS-51
// core, behind IN #FE (tdd-atm2-kbc.md §3, §5; reference-atm2-kbc.md §2.4)

#include <gtest/gtest.h>

#include "_helpers/networksettings.h"

#include <cstring>
#include <memory>

#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/atm/ttdatm2kbc.h"
#include "debugger/ttd/network/ttdmachineserialpeer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/ports/models/portdecoder_atm710.h"

class Atm2Kbc_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create(const char* model, int ram)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("atm2kbc", model, ram, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    Atm2Kbc* Kbc()
    {
        auto* decoder = dynamic_cast<PortDecoder_ATM710*>(_context->pPortDecoder);
        return decoder ? decoder->GetKeyboardController() : nullptr;
    }

    /// Fit `firmware` and let it finish its power-on (2.2 / 3.1: the self reset; 4.x: 120 ms delay)
    void Fit(Atm2Kbc::Firmware firmware)
    {
        Atm2Kbc* kbc = Kbc();
        ASSERT_NE(kbc, nullptr);
        std::string error;
        ASSERT_TRUE(kbc->Load(firmware, "", error)) << error;
        _emulator->RunNFrames(9);
    }

    uint8_t In(uint16_t port) { return _context->pCore->GetZ80()->in(port); }

    /// IN with the Z80's T-state cost, in microseconds of the current clock
    double TimedInUs(uint16_t port, uint8_t& value)
    {
        Z80* z80 = _context->pCore->GetZ80();
        const uint32_t before = z80->t;
        value = z80->in(port);
        return (z80->t - before) * 1e6 / static_cast<double>(_context->emulatorState.current_z80_frequency);
    }
};

TEST_F(Atm2Kbc_Test, TheV710BoardHasItTheOthersDoNot)
{
    Create("ATM710", 1024);
    ASSERT_NE(Kbc(), nullptr) << "ATM Turbo 2+ v7.xx: the keyboard controller by default";
    EXPECT_EQ(Kbc()->GetFirmware(), Atm2Kbc::Firmware::V41);
    EXPECT_EQ(Kbc()->Cpu()->GetVariant(), mcs51::Mcs51::Variant::I8052) << "v4.x runs on an AT89S52";
    _manager->RemoveEmulator(_emulator->GetId());
    _emulator.reset();

    Create("ATM3", 4096);
    EXPECT_EQ(Kbc(), nullptr) << "the ZX-Evo's keyboard is the AVR";
}

TEST_F(Atm2Kbc_Test, EveryImageAnswersTheEscapeAndItsVersion)
{
    // Booting each image: about 60 ms of controller time each
    Create("ATM710", 1024);
    struct Row
    {
        Atm2Kbc::Firmware firmware;
        uint8_t version[4];
    };
    const Row rows[] = {
        {Atm2Kbc::Firmware::V22At7, {2, 2, 0, 7}},  {Atm2Kbc::Firmware::V22At11, {2, 2, 1, 1}},
        {Atm2Kbc::Firmware::V22At12, {2, 2, 1, 2}}, {Atm2Kbc::Firmware::V31At7, {3, 1, 0, 7}},
        {Atm2Kbc::Firmware::V31At11, {3, 1, 1, 1}}, {Atm2Kbc::Firmware::V32At7, {3, 2, 0, 7}},
        {Atm2Kbc::Firmware::V32At11, {3, 2, 1, 1}}, {Atm2Kbc::Firmware::V40, {4, 0, 1, 1}},
        {Atm2Kbc::Firmware::V41, {4, 1, 1, 1}},
    };
    for (const Row& row : rows)
    {
        SCOPED_TRACE(Atm2Kbc::FirmwareName(row.firmware));
        Fit(row.firmware);
        for (int x = 0; x < 4; ++x)
        {
            EXPECT_EQ(In(0x55FE), 0xAA) << "#55 arms the command mode";
            EXPECT_EQ(In(static_cast<uint16_t>(((x << 6) | 0x01) << 8 | 0xFE)), row.version[x]) << "version byte " << x;
        }
        EXPECT_EQ(In(0x55FE), 0xAA);
        EXPECT_EQ(In(0x00FE), 0xFF) << "command 0: NOP";
    }
}

TEST_F(Atm2Kbc_Test, EveryReadWaitsForTheController)
{
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V32At7);
    uint8_t value = 0;
    // reference §2.7 (hand count of v3.2 at 7 MHz): #55 48-58 us, a command read 98-108 us
    const double escape = TimedInUs(0x55FE, value);
    EXPECT_EQ(value, 0xAA);
    EXPECT_GT(escape, 40.0);
    EXPECT_LT(escape, 70.0);
    const double command = TimedInUs(0x41FE, value);
    EXPECT_EQ(value, 2) << "version byte 1";
    EXPECT_GT(command, escape) << "a command takes longer than the escape";

    // At 11.0592 MHz the same firmware answers ~0.63x as fast
    Fit(Atm2Kbc::Firmware::V32At11);
    const double fast = TimedInUs(0x55FE, value);
    EXPECT_EQ(value, 0xAA);
    EXPECT_LT(fast, escape * 0.8);
}

TEST_F(Atm2Kbc_Test, Ve1TurnsTheControllerOff)
{
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    // #FD77: A9 = 0 keeps the system ports open (~CPM); VE0 = 1 as the doc asks
    _context->pPortDecoder->DecodePortOut(0xFD77, 0x40 | 0x80 | 0x03, 0);
    _emulator->RunNFrames(1);
    uint8_t value = 0;
    const double us = TimedInUs(0x55FE, value);
    EXPECT_LT(us, 5.0) << "no WAIT: the read goes to the ZX keyboard";
    EXPECT_NE(value, 0xAA);
    _emulator->RunNFrames(1);
    EXPECT_TRUE(Kbc()->Cpu()->Latch(1) & 0x80) << "the firmware parked with W_ON = 1";
    _context->pPortDecoder->DecodePortOut(0xFD77, 0x80 | 0x03, 0);
    _emulator->RunNFrames(1);
    EXPECT_FALSE(Kbc()->Cpu()->Latch(1) & 0x80) << "VE1 = 0: WAIT generation back";
    EXPECT_EQ(In(0x55FE), 0xAA) << "back on";
}

TEST_F(Atm2Kbc_Test, ABlockedControllerSeesNoEscape)
{
    // Ctrl+Alt+Ins (keypad 0) blocks v4.x: W_ON = 1, the board holds no read, so the 8031's INT1 handler runs
    // after the Z80's cycle and reads the floating bus (#FF), not A15..A8. A #55FE poll cannot arm the command
    // mode, which Ctrl+Alt+Home (keypad 7) would carry over: it does not clear R7 (atm_at41.asm 523-549,
    // reference-atm2-kbc.md (b) item 8). Slower than 50 ms: the firmware's 120 ms power-on delay and two
    // typed chords run in emulated frames
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    auto chord = [this](PcKey key) {
        for (PcKey k : {PcKey::LeftCtrl, PcKey::LeftAlt, key})
        {
            Kbc()->OnPcKey(k, true);
            _emulator->RunNFrames(2);
        }
        for (PcKey k : {key, PcKey::LeftAlt, PcKey::LeftCtrl})
        {
            Kbc()->OnPcKey(k, false);
            _emulator->RunNFrames(2);
        }
    };
    chord(PcKey::Keypad0);
    ASSERT_TRUE(Kbc()->Cpu()->Latch(1) & 0x80) << "blocked: W_ON = 1";
    uint8_t value = 0;
    EXPECT_LT(TimedInUs(0x55FE, value), 5.0) << "no WAIT while blocked";
    _emulator->RunNFrames(1);
    // The handler did run (INT1), but on #FF: the firmware's command flag (R7 of register bank 1) stays clear
    EXPECT_EQ(Kbc()->Cpu()->Ram(0x0F), 0) << "R7: no #55 seen while blocked";
    chord(PcKey::Keypad7);
    ASSERT_FALSE(Kbc()->Cpu()->Latch(1) & 0x80) << "unblocked";
    EXPECT_EQ(In(0x55FE), 0xAA);
    EXPECT_EQ(In(0x01FE), 4) << "a served #55 arms it";
}

TEST_F(Atm2Kbc_Test, Mode0PassesTheZxKeyboard)
{
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    EXPECT_EQ(In(0x7FFE) & 0x1F, 0x1F) << "no key";
    _context->pKeyboard->PressKey(ZXKEY_SPACE);
    EXPECT_EQ(In(0x7FFE) & 0x01, 0x00) << "mode 0: the native port AND the controller's keys";
    _context->pKeyboard->ReleaseKey(ZXKEY_SPACE);
    EXPECT_EQ(In(0x7FFE) & 0x1F, 0x1F);
}

TEST_F(Atm2Kbc_Test, NoneKeepsThePlainPort)
{
    Create("ATM710", 1024);
    std::string error;
    ASSERT_TRUE(Kbc()->Load(Atm2Kbc::Firmware::None, "", error));
    EXPECT_EQ(Kbc(), nullptr);
}

TEST_F(Atm2Kbc_Test, ThePcKeyboardReachesTheMatrix)
{
    // A PS/2 key: set-2 frames on the controller's clock / data lines, the
    // firmware turns them into Spectrum matrix bits (mode 0)
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    _context->pKeyboard->ApplyPcKey(PcKey::A, true);
    _emulator->RunNFrames(2);
    EXPECT_EQ(In(0xFDFE) & 0x01, 0x00) << "PC A -> ZX A (row #FDFE bit 0)";
    EXPECT_EQ(In(0x7FFE) & 0x1F, 0x1F) << "no other row";
    _context->pKeyboard->ApplyPcKey(PcKey::A, false);
    _emulator->RunNFrames(2);
    EXPECT_EQ(In(0xFDFE) & 0x01, 0x01);
}

TEST_F(Atm2Kbc_Test, Mode3ReturnsTheLastScanCode)
{
    // Mode 3: a plain read gives the last scan code in set 1 (the firmware's at2xt table)
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    In(0x55FE);
    In(0x08FE);
    In(0x03FE);   // mode 3
    _context->pKeyboard->ApplyPcKey(PcKey::A, true);
    _emulator->RunNFrames(2);
    EXPECT_EQ(In(0x00FE), 0x1E) << "A make: set 1 code 1E";
    _context->pKeyboard->ApplyPcKey(PcKey::A, false);
    _emulator->RunNFrames(2);
}

TEST_F(Atm2Kbc_Test, AHeldKeyRepeatsLikeAPcKeyboard)
{
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    _context->pKeyboard->ApplyPcKey(PcKey::B, true);
    _emulator->RunNFrames(5);
    EXPECT_EQ(Kbc()->GetKeyboard().repeatKey, PcKey::B);
    const uint64_t first = Kbc()->GetKeyboard().repeatAt;
    _emulator->RunNFrames(40);   // 0.8 s: past the 500 ms delay, a few repeats at 10.9 / s
    EXPECT_GT(Kbc()->GetKeyboard().repeatAt, first + Kbc()->CrystalHz() / 10) << "repeats were sent";
    _context->pKeyboard->ApplyPcKey(PcKey::B, false);
    _emulator->RunNFrames(1);
    EXPECT_EQ(Kbc()->GetKeyboard().repeatKey, PcKey::None);
}

TEST_F(Atm2Kbc_Test, TtdBlobRoundTrip)
{
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    Atm2Kbc* kbc = Kbc();
    auto saved = std::make_unique<Atm2Kbc::State>();
    kbc->SaveState(*saved);
    const uint16_t pc = kbc->Cpu()->Pc();

    _context->pKeyboard->ApplyPcKey(PcKey::Q, true);
    _emulator->RunNFrames(3);
    In(0x55FE);
    In(0x08FE);
    In(0x02FE);   // mode 2
    ASSERT_NE(kbc->Cpu()->Clock(), saved->cpu.clock);

    ASSERT_TRUE(kbc->LoadState(*saved));
    EXPECT_EQ(kbc->Cpu()->Clock(), saved->cpu.clock);
    EXPECT_EQ(kbc->Cpu()->Pc(), pc);
    auto again = std::make_unique<Atm2Kbc::State>();
    kbc->SaveState(*again);
    EXPECT_EQ(std::memcmp(saved.get(), again.get(), sizeof(Atm2Kbc::State)), 0) << "the blob comes back byte for byte";

    saved->firmware = static_cast<uint8_t>(Atm2Kbc::Firmware::V32At7);
    EXPECT_FALSE(kbc->LoadState(*saved)) << "a blob of another image is refused";
    _context->pKeyboard->ApplyPcKey(PcKey::Q, false);
}

TEST_F(Atm2Kbc_Test, TtdSeekReplaysTheControllerExactly)
{
    // Record a PC key through the controller, seek back, replay: the
    // controller ends in the recorded state
    Create("ATM710", 1024);
    Fit(Atm2Kbc::Firmware::V41);
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(2);
    const uint64_t before = _context->emulatorState.frame_counter;
    _context->pDebugManager->GetKeyboardManager()->TapKey("pc.w", 3);
    _emulator->RunNFrames(12);
    const uint64_t end = _context->emulatorState.frame_counter;
    auto recorded = std::make_unique<Atm2Kbc::State>();
    Kbc()->SaveState(*recorded);
    ttd->StopRecording();

    ASSERT_TRUE(ttd->SeekTo({before, 0}));
    _emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(_context->emulatorState.frame_counter, end);
    auto replayed = std::make_unique<Atm2Kbc::State>();
    Kbc()->SaveState(*replayed);
    EXPECT_EQ(replayed->cpu.clock, recorded->cpu.clock);
    EXPECT_EQ(replayed->cpu.pc, recorded->cpu.pc);
    EXPECT_EQ(std::memcmp(replayed->cpu.ram, recorded->cpu.ram, sizeof(recorded->cpu.ram)), 0);
    EXPECT_EQ(std::memcmp(replayed.get(), recorded.get(), sizeof(Atm2Kbc::State)), 0) << "the whole controller as recorded";
}

// --- RS-232: the controller's UART is the machine's serial port (tdd-atm2-kbc.md §7) ---

class Atm2KbcSerial_Test : public Atm2Kbc_Test
{
protected:
    /// The board with V41 booted and `settings` applied through the network manager
    void CreateWith(const std::vector<std::pair<std::string, std::string>>& settings)
    {
        Create("ATM710", 1024);
        _emulator->RunNFrames(9);   // v4.x: 120 ms power-on delay
        // As every surface applies them: a ZX-bus card restarts the machine (Q11), which boots the controller again.
        // A ZX-bus card behind the ATM Turbo 2+ CPU-socket adapter is an unrealistic fit (Q5): the flag accepts it
        const std::string before = _emulator->GetId();
        const SlotControlReply reply = NetworkSettings::Apply(_emulator, settings, true);
        ASSERT_TRUE(reply.Ok()) << reply.message;
        _context = _emulator->GetContext();
        if (_emulator->GetId() != before)
            _emulator->RunNFrames(9);
    }

    /// An IN #FE as a driver loop issues it: ~40 T of its own code first. Back
    /// to back reads (no gap) keep INT1 pending at every RETI, and INT1 comes
    /// before the UART in the 8051 polling order: received frames would be
    /// lost, on the real board too (reference-atm2-kbc.md §2.5)
    uint8_t DriverIn(uint16_t port)
    {
        _context->pCore->GetZ80()->t += 40;
        return In(port);
    }

    /// `#55`, command, and the argument byte of a 2-stage command
    void Command(uint8_t command, int argument = -1)
    {
        ASSERT_EQ(DriverIn(0x55FE), 0xAA);
        const uint8_t answer = DriverIn(static_cast<uint16_t>(command << 8 | 0xFE));
        if (argument >= 0)
        {
            EXPECT_EQ(answer, 0xFF);
            EXPECT_EQ(DriverIn(static_cast<uint16_t>(argument << 8 | 0xFE)), 0xFF);
        }
    }
    uint8_t Query(uint8_t command)
    {
        EXPECT_EQ(DriverIn(0x55FE), 0xAA);
        return DriverIn(static_cast<uint16_t>(command << 8 | 0xFE));
    }
};

TEST_F(Atm2KbcSerial_Test, ComPortPlugsIntoTheController)
{
    CreateWith({{"com_port", "loopback"}});
    ASSERT_NE(_context->pMachineSerialPeer, nullptr);
    EXPECT_EQ(Kbc()->SerialPeer(), _context->pMachineSerialPeer);
    EXPECT_STREQ(_context->pMachineSerialPeer->Kind(), "loopback");
    EXPECT_EQ(_context->pComPort, nullptr) << "no 16550 on #xxEF: the line is the MCU's UART";

    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.serialPort, "atm2-kbc");
    EXPECT_TRUE(st.machineSerial.fitted);
    EXPECT_EQ(st.machineSerial.firmware, "V41");
    EXPECT_EQ(st.machineSerial.peer, "loopback");
}

TEST_F(Atm2KbcSerial_Test, AByteGoesOutAndComesBack)
{
    CreateWith({{"com_port", "loopback"}});
    Command(0xC3, 1);      // divisor 1: 115200 baud (v4.x, timer 2)
    Command(0x43, 0x03);   // DTR + RTS asserted: the peer may send
    EXPECT_EQ(Kbc()->SerialBaud(), 115200u);
    EXPECT_TRUE(Kbc()->Rts());
    EXPECT_TRUE(Kbc()->Dtr());

    Command(0x03, 0x5A);   // TX data
    _emulator->RunNFrames(1);   // 2 frames on the wire (~174 us): well inside one 20 ms frame

    EXPECT_EQ(Kbc()->GetSerialLine().bytesOut, 1u);
    EXPECT_EQ(Kbc()->GetSerialLine().bytesIn, 1u);
    EXPECT_EQ(Query(0xC2), 1) << "RX count";
    EXPECT_EQ(Query(0x02), 0x5A) << "RX data: the loopback echo";
    EXPECT_EQ(Query(0xC2), 0);
}

TEST_F(Atm2KbcSerial_Test, RtsOffHoldsThePeer)
{
    CreateWith({{"com_port", "loopback"}});
    Command(0xC3, 1);
    Command(0x43, 0x01);   // DTR only
    Command(0x03, 0x33);
    _emulator->RunNFrames(1);
    EXPECT_EQ(Kbc()->GetSerialLine().bytesIn, 0u) << "RTS deasserted: the peer waits";
    EXPECT_EQ(_context->pMachineSerialPeer->Pending(), 1u);

    Command(0x43, 0x03);
    _emulator->RunNFrames(1);
    EXPECT_EQ(Query(0x02), 0x33);
}

TEST_F(Atm2KbcSerial_Test, ZxWifiFitsBeside)
{
    CreateWith({{"com_port", "loopback"}, {"card", "zxwifi"}});
    EXPECT_NE(_context->pMachineSerialPeer, nullptr);
    ASSERT_NE(_context->pComPort, nullptr) << "the controller is not on #xxEF: the card's 16550 fits";
}

TEST_F(Atm2KbcSerial_Test, AFirmwareWithoutRs232HasNoPort)
{
    CreateWith({{"com_port", "loopback"}, {"kbc_firmware", "v22-11"}});
    ASSERT_NE(Kbc(), nullptr);
    EXPECT_EQ(Kbc()->GetFirmware(), Atm2Kbc::Firmware::V22At11);
    EXPECT_EQ(_context->pMachineSerialPeer, nullptr);
    EXPECT_EQ(Kbc()->SerialPeer(), nullptr);
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    ASSERT_FALSE(st.notes.empty());
    EXPECT_NE(st.notes.front().find("RS-232"), std::string::npos) << st.notes.front();

    // Back to a firmware with the port: the peer is plugged in again
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"kbc_firmware", "v41"}}, change, error)) << error;
    ASSERT_TRUE(_context->pCore->GetNetworkManager()->RequestChange(change, error)) << error;
    EXPECT_EQ(Kbc()->GetFirmware(), Atm2Kbc::Firmware::V41);
    EXPECT_NE(_context->pMachineSerialPeer, nullptr);
    EXPECT_EQ(Kbc()->SerialPeer(), _context->pMachineSerialPeer);
}

TEST_F(Atm2KbcSerial_Test, TtdBlobKeepsThePeer)
{
    CreateWith({{"com_port", "loopback"}});
    Command(0xC3, 1);
    Command(0x43, 0x01);   // RTS off: the echo stays in the peer
    Command(0x03, 0x77);
    _emulator->RunNFrames(1);
    auto* loop = dynamic_cast<LoopbackPeer*>(_context->pMachineSerialPeer);
    ASSERT_NE(loop, nullptr);
    ASSERT_EQ(loop->Queue().size(), 1u);

    ttd::TTDMachineSerialPeer serializer(_context);
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    loop->Reset();
    serializer.TTDLoadState(blob.data());
    ASSERT_EQ(loop->Queue().size(), 1u);
    EXPECT_EQ(loop->Queue().front(), 0x77);
}

TEST_F(Atm2KbcSerial_Test, AnEspModuleRateCanBeGiven)
{
    // A module built for another rate: ComPort=ESPNET,115200
    CreateWith({{"com_port", "espnet,115200"}});
    auto* module = dynamic_cast<EspModule*>(_context->pMachineSerialPeer);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Baud(), 115200u);
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.machineSerial.peerBaud, 115200u);
    EXPECT_EQ(st.settings.comPort, "ESPNET,115200");
}

TEST_F(Atm2KbcSerial_Test, AnEspModuleAnswersThroughTheController)
{
    // The emulated ESP (AT firmware) on the controller's RS-232: "AT" -> "OK".
    // Paced as the drivers do it (reference-atm2-kbc.md §2.5: reading the
    // buffer out is slower than 115200 baud): the command goes out with RTS
    // off, then RTS is on while the driver waits a frame and off while the
    // buffer is read out. The Z80 waits in a loop of its own: a program that
    // polls the keyboard meanwhile can cost a byte at 115200 (an INT1 answer
    // of up to 83 cycles plus the 36-cycle timer 0 tick outlast one 80-cycle
    // frame; both come before the UART in the 8051 polling order)
    CreateWith({{"com_port", "at"}});
    ASSERT_NE(_context->pMachineSerialPeer, nullptr);
    auto* module = dynamic_cast<EspModule*>(_context->pMachineSerialPeer);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Baud(), 38400u) << "the ATM2 COM build: the port's default rate";
    Z80* z80 = _context->pCore->GetZ80();
    _context->pMemory->DirectWriteToZ80Memory(0x8000, 0xF3);   // DI
    _context->pMemory->DirectWriteToZ80Memory(0x8001, 0x18);   // JR $
    _context->pMemory->DirectWriteToZ80Memory(0x8002, 0xFE);
    z80->pc = 0x8000;

    Command(0xC3, 3);      // divisor 3: 38400 baud, the module's rate on this port
    Command(0x43, 0x01);   // DTR only
    for (char c : std::string("AT\r\n"))
        Command(0x03, static_cast<uint8_t>(c));   // the firmware queues them (64-byte TX ring on v4)

    std::string reply;
    for (int frame = 0; frame < 10 && reply.find("OK") == std::string::npos; ++frame)
    {
        Command(0x43, 0x03);   // RTS on: the module may talk
        _emulator->RunNFrames(1);
        Command(0x43, 0x01);   // RTS off while reading out
        for (uint8_t n = Query(0xC2); n > 0; --n)
            reply.push_back(static_cast<char>(Query(0x02)));
    }
    EXPECT_NE(reply.find("OK"), std::string::npos) << "reply: " << reply;
    EXPECT_EQ(Kbc()->GetSerialLine().lost, 0u) << "reply: " << reply;
    EXPECT_EQ(Kbc()->GetSerialLine().bytesOut, 4u);
}

/// TTD: the firmware image is configuration, not state; the controller's
/// descriptor carries its fingerprint, so a restore on another image is
/// reported (FirmwareDiffers) rather than silently replaying differently
TEST_F(Atm2Kbc_Test, TheDescriptorCarriesTheFirmwareFingerprint)
{
    Create("ATM710", 1024);
    ASSERT_NE(Kbc(), nullptr);
    ttd::TTDAtm2Kbc device(*Kbc());
    const uint64_t v41 = device.TTDDescribe().firmwareFingerprint;
    EXPECT_NE(v41, 0u);

    std::string error;
    ASSERT_TRUE(Kbc()->Load(Atm2Kbc::Firmware::V40, "", error)) << error;
    ttd::TTDAtm2Kbc v40Device(*Kbc());
    EXPECT_NE(v40Device.TTDDescribe().firmwareFingerprint, v41) << "another image, another fingerprint";
    ASSERT_TRUE(Kbc()->Load(Atm2Kbc::Firmware::V41, "", error)) << error;
    ttd::TTDAtm2Kbc again(*Kbc());
    EXPECT_EQ(again.TTDDescribe().firmwareFingerprint, v41) << "the same image, the same fingerprint";
}
