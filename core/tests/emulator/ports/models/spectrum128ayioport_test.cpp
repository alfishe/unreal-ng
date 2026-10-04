// The board wiring on AY I/O port A of the Sinclair 128K family (Spectrum128AyIoPort): what a read of R14 returns on
// the 128K, the grey +2 and the +2A / +3 with nothing plugged in and with a device holding receiver lines low, and
// that no other machine and no second TurboSound chip sees any of it.
// docs/inprogress/2026-10-04-ay-reset/TODO.md item 4

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include "emulator/ports/models/spectrum128ayioport.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/loader_z80.h"

namespace
{
constexpr uint8_t kR7PortAOutput = 0b0111'1111;  // port A output, port B input, generators off
constexpr uint8_t kR7BothInputs = 0b0011'1111;   // both ports inputs, generators off

/// One machine with its TurboSound slot pinned to `kind`, IN / OUT through the machine's own port decoder
class Machine
{
public:
    Machine(const std::string& model, TurboSoundKind kind)
        : _emulator(EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(model, kind))
    {
        if (_emulator)
            _context = _emulator->GetContext();
    }
    ~Machine()
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    bool Ok() const
    {
        return _context && _context->pPortDecoder && _context->pSoundManager;
    }
    EmulatorContext* Context() const
    {
        return _context;
    }
    SoundChip_AY8910* Chip(int index) const
    {
        return _context->pSoundManager->getAYChip(index);
    }

    void Write(uint8_t reg, uint8_t value)
    {
        _context->pPortDecoder->DecodePortOut(0xFFFD, reg, 0x8000);
        _context->pPortDecoder->DecodePortOut(0xBFFD, value, 0x8000);
    }
    uint8_t Read(uint8_t reg)
    {
        _context->pPortDecoder->DecodePortOut(0xFFFD, reg, 0x8000);
        return _context->pPortDecoder->DecodePortIn(0xFFFD, 0x8000);
    }

    /// The machine's port A wiring (the 128K family only)
    Spectrum128AyIoPort* Wiring() const
    {
        if (auto* d = dynamic_cast<PortDecoder_Spectrum128*>(_context->pPortDecoder))
            return &d->AyIoPort();
        if (auto* d = dynamic_cast<PortDecoder_Spectrum3*>(_context->pPortDecoder))
            return &d->AyIoPort();
        return nullptr;
    }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
};

struct BoardCase
{
    const char* model;
    TurboSoundKind kind;
};

std::string CaseName(const ::testing::TestParamInfo<BoardCase>& info)
{
    std::string name = info.param.model;
    name += info.param.kind == TurboSoundKind::FM ? "_TSFM" : "_AY";
    return name;
}
}  // namespace

/// region <128K family>

class Spectrum128AyIoPort_Test : public ::testing::TestWithParam<BoardCase>
{
};

/// The board wires the AY in its socket (chip 0 of the slot: the AY, or the first chip of a TurboSound / TSFM board);
/// the second chip's pins are unconnected
TEST_P(Spectrum128AyIoPort_Test, TheSocketChipCarriesTheBoardWiring)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());
    ASSERT_NE(m.Wiring(), nullptr);
    EXPECT_EQ(m.Chip(0)->ioPortInput(), m.Wiring());
    if (SoundChip_AY8910* second = m.Chip(1))
        EXPECT_EQ(second->ioPortInput(), nullptr) << "the second chip's I/O pins are not on the socket";
}

/// Nothing plugged in: the 1489 receiver outputs are high and the 1488 driver inputs do not pull, so every pin is high
/// (a real 128K prints 255 for OUT 65533,14: PRINT IN 65533). Fuse's #BF is not the hardware
TEST_P(Spectrum128AyIoPort_Test, NothingConnectedReadsThePullUpsAndTheLatch)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());

    m.Write(AY_MIXER_CONTROL, kR7BothInputs);
    m.Write(AY_PORTA, 0x5A);
    EXPECT_EQ(m.Read(AY_PORTA), 0xFF) << "input: the pins";
    EXPECT_EQ(m.Read(AY_PORTB), 0xFF) << "R15: no pins on the 8912, the pull-ups";

    m.Write(AY_MIXER_CONTROL, kR7PortAOutput);
    EXPECT_EQ(m.Read(AY_PORTA), 0x5A) << "output: the latch, nothing pulls against it";
    m.Write(AY_PORTA, 0xFF);
    EXPECT_EQ(m.Read(AY_PORTA), 0xFF) << "the ROM's state (R7 = #FF, R14 = #FF)";
}

/// A device that holds receiver lines low (e.g. a printer asserting DTR = ready, bit 6) shows in both directions: a
/// pin held low reads 0 (the AY's output high is a weak pull-up, the receiver wins). Bits 0-3 belong to the driver
/// inputs and are never the board's to pull
TEST_P(Spectrum128AyIoPort_Test, ReceiverLinesHeldLowReadLowInBothDirections)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());
    Spectrum128AyIoPort* wiring = m.Wiring();
    ASSERT_NE(wiring, nullptr);

    wiring->SetReceiverOutputs(static_cast<uint8_t>(~Spectrum128AyIoPort::BitRs232Dtr));
    m.Write(AY_MIXER_CONTROL, kR7BothInputs);
    EXPECT_EQ(m.Read(AY_PORTA), 0xBF) << "input: DTR ready";
    m.Write(AY_MIXER_CONTROL, kR7PortAOutput);
    m.Write(AY_PORTA, 0xFF);
    EXPECT_EQ(m.Read(AY_PORTA), 0xBF) << "output: the latch AND the line";
    m.Write(AY_PORTA, 0x0F);
    EXPECT_EQ(m.Read(AY_PORTA), 0x0F);

    wiring->SetReceiverOutputs(0x00);
    EXPECT_EQ(wiring->ReceiverOutputs(), 0x0F) << "only bits 4-7 come from the receiver";
    m.Write(AY_MIXER_CONTROL, kR7BothInputs);
    EXPECT_EQ(m.Read(AY_PORTA), 0x0F);
    EXPECT_EQ(m.Read(AY_PORTB), 0xFF) << "port B is not wired";

    EXPECT_EQ(m.Chip(0)->readRegister(AY_PORTA), 0x0F) << "the register file keeps the latch (snapshots, debuggers)";
}

/// A machine reset clears the AY registers but keeps the board: the wiring stays attached, the lines keep their levels
TEST_P(Spectrum128AyIoPort_Test, MachineResetKeepsTheWiring)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());
    m.Wiring()->SetReceiverOutputs(static_cast<uint8_t>(~Spectrum128AyIoPort::BitRs232Txd));

    m.Context()->pEmulator->Reset();

    EXPECT_EQ(m.Chip(0)->ioPortInput(), m.Wiring());
    EXPECT_EQ(m.Read(AY_PORTA), 0x7F) << "reset: port A an input, TXD held low";
}

/// TTD restore and snapshots carry the register file only: the latch goes out and comes back, the pin levels are
/// the board's at read time
TEST_P(Spectrum128AyIoPort_Test, TtdAndSnapshotRoundTripsKeepTheLatch)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());
    SoundChip_AY8910* chip = m.Chip(0);
    m.Wiring()->SetReceiverOutputs(0x0F);
    m.Write(AY_MIXER_CONTROL, kR7PortAOutput);
    m.Write(AY_PORTA, 0x5A);
    ASSERT_EQ(m.Read(AY_PORTA), 0x0A);

    // TTD: the blob holds the latch; a restore leaves the wiring attached
    std::vector<uint8_t> blob(chip->TTDStateSize());
    chip->TTDSaveState(blob.data());
    m.Write(AY_PORTA, 0xFF);
    chip->TTDLoadState(blob.data());
    EXPECT_EQ(chip->readRegister(AY_PORTA), 0x5A);
    EXPECT_EQ(chip->ioPortInput(), m.Wiring());
    EXPECT_EQ(m.Read(AY_PORTA), 0x0A);

    // .z80: the saved R14 byte is the latch, not the pins
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("ay_ioport_wiring.z80");
    {
        LoaderZ80 saver(m.Context(), path);
        ASSERT_TRUE(saver.save());
    }
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    constexpr size_t kZ80AyRegisters = 39;  // v2 / v3 extended header: 16 AY registers
    ASSERT_GE(bytes.size(), kZ80AyRegisters + 16);
    EXPECT_EQ(bytes[kZ80AyRegisters + AY_PORTA], 0x5A);
    EXPECT_EQ(bytes[kZ80AyRegisters + AY_MIXER_CONTROL], kR7PortAOutput);

    m.Write(AY_PORTA, 0xFF);
    {
        LoaderZ80 loader(m.Context(), path);
        ASSERT_TRUE(loader.load());
    }
    std::remove(path.c_str());
    EXPECT_EQ(m.Chip(0)->readRegister(AY_PORTA), 0x5A);
    EXPECT_EQ(m.Chip(0)->ioPortInput(), m.Wiring());
    EXPECT_EQ(m.Read(AY_PORTA), 0x0A);
}

INSTANTIATE_TEST_SUITE_P(Boards, Spectrum128AyIoPort_Test,
                         ::testing::Values(BoardCase{"128k", TurboSoundKind::AY}, BoardCase{"PLUS2", TurboSoundKind::AY},
                                           BoardCase{"PLUS2A", TurboSoundKind::AY},
                                           BoardCase{"PLUS3", TurboSoundKind::AY},
                                           BoardCase{"128k", TurboSoundKind::FM},
                                           BoardCase{"PLUS3", TurboSoundKind::FM}),
                         CaseName);

/// endregion </128K family>

/// region <Every other machine>

class AyIoPortUnwired_Test : public ::testing::TestWithParam<BoardCase>
{
};

/// No other board attaches anything: every chip of the slot has no wiring, an input port reads the pull-ups and an
/// output port its latch, exactly as before
TEST_P(AyIoPortUnwired_Test, ChipAloneReadsPullUpsAndLatch)
{
    Machine m(GetParam().model, GetParam().kind);
    ASSERT_TRUE(m.Ok());
    for (int i = 0; i < m.Context()->pSoundManager->getAYChipCount(); i++)
        EXPECT_EQ(m.Chip(i)->ioPortInput(), nullptr) << "chip " << i;

    m.Write(AY_MIXER_CONTROL, kR7BothInputs);
    m.Write(AY_PORTA, 0x5A);
    m.Write(AY_PORTB, 0x3C);
    EXPECT_EQ(m.Read(AY_PORTA), 0xFF);
    EXPECT_EQ(m.Read(AY_PORTB), 0xFF);
    m.Write(AY_MIXER_CONTROL, 0xFF);
    EXPECT_EQ(m.Read(AY_PORTA), 0x5A);
    EXPECT_EQ(m.Read(AY_PORTB), 0x3C);
}

INSTANTIATE_TEST_SUITE_P(Machines, AyIoPortUnwired_Test,
                         ::testing::Values(BoardCase{"48K", TurboSoundKind::AY},
                                           BoardCase{"PENTAGON", TurboSoundKind::AY},
                                           BoardCase{"PENTAGON", TurboSoundKind::FM},
                                           BoardCase{"SCORPION", TurboSoundKind::AY},
                                           BoardCase{"TSL", TurboSoundKind::FM}),
                         CaseName);

/// endregion </Every other machine>
