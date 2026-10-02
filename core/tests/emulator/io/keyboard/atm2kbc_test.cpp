// ATM Turbo 2+ keyboard controller: the real firmware images on the MCS-51
// core, behind IN #FE (tdd-atm2-kbc.md §3, §5; reference-atm2-kbc.md §2.4)

#include <gtest/gtest.h>

#include <memory>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
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
