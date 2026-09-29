// Board adapters (IDE design §12.2, layer 2): for every scheme, which ports
// reach which ATA register, when the board answers, and how the data word is
// split. The unit is a disk over a MemoryDisk; register values are read back
// through the task file, words through the image bytes

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "emulator/config.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ideadapter.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/memorydisk.h"

using namespace ata;

namespace
{
    /// One machine's IDE board with a hard disk as master
    struct Board
    {
        EmulatorContext context{LoggerLevel::LogError};
        std::unique_ptr<IdeController> ide;
        MemoryDisk disk{256};
        std::unique_ptr<IdeAdapter> adapter;
        IdeAdapter::Gate on;   ///< the gate state in which the board answers
        IdeAdapter::Gate off;  ///< ... and in which it does not

        Board(IDE_SCHEME scheme, MEM_MODEL model)
        {
            context.config.mem_model = model;
            context.config.ide_scheme = scheme;
            ide = std::make_unique<IdeController>(&context);
            context.pIdeController = ide.get();
            ide->Channel().Unit(0)->AttachMedium(disk, {});
            adapter = std::make_unique<IdeAdapter>(&context);
            for (uint64_t lba = 0; lba < 256; lba++)
                std::memset(disk.Data() + lba * 512, static_cast<int>(lba), 512);
        }
        ~Board() { context.pIdeController = nullptr; }

        AtaDevice& Master() { return *ide->Channel().Unit(0); }

        uint8_t In(uint16_t port, const IdeAdapter::Gate& gate)
        {
            uint8_t value = 0;
            EXPECT_TRUE(adapter->In(port, gate, value)) << std::hex << port;
            return value;
        }
        void Out(uint16_t port, uint8_t value, const IdeAdapter::Gate& gate)
        {
            EXPECT_TRUE(adapter->Out(port, gate, value)) << std::hex << port;
        }
        bool Claims(uint16_t port, const IdeAdapter::Gate& gate)
        {
            uint8_t value = 0;
            return adapter->In(port, gate, value);
        }
    };

    IdeAdapter::Gate Dos(bool on)
    {
        IdeAdapter::Gate gate;
        gate.dosPorts = on;
        return gate;
    }

    IdeAdapter::Gate ProfiExt(bool on)
    {
        IdeAdapter::Gate gate;
        gate.profiExt = on;
        return gate;
    }

    /// The board's port for task-file register `reg` when A7..A5 select it
    uint16_t A7A5(uint8_t reg, uint8_t low) { return static_cast<uint16_t>((reg << 5) | low); }
    /// ... when A10..A8 select it
    uint16_t A10A8(uint8_t reg, uint16_t base) { return static_cast<uint16_t>((reg << 8) | base); }

    /// Start a WRITE SECTORS of one sector at LBA 5 through `write`
    template <typename Write>
    void StartWrite(Write write)
    {
        write(DeviceHead, 0xE0);
        write(SectorCount, 1);
        write(SectorNumber, 5);
        write(CylinderLow, 0);
        write(CylinderHigh, 0);
        write(StatusCommand, Command::WriteSectors);
    }

    /// Start a READ SECTORS of one sector at LBA 9
    template <typename Write>
    void StartRead(Write write)
    {
        write(DeviceHead, 0xE0);
        write(SectorCount, 1);
        write(SectorNumber, 9);
        write(CylinderLow, 0);
        write(CylinderHigh, 0);
        write(StatusCommand, Command::ReadSectors);
    }
}  // namespace

TEST(IdeAdapter_Test, NemoDecodeGateAndLatch)
{
    Board b(IDE_NEMO, MM_PENTAGON);
    b.on = Dos(false);
    b.off = Dos(true);

    // Task file: A7..A5 = register, A4 A3 = 1 0 (CS0)
    b.Out(A7A5(SectorCount, 0x10), 0x42, b.on);
    EXPECT_EQ(b.Master().State().sectorCount, 0x42);
    EXPECT_EQ(b.In(A7A5(SectorCount, 0x10), b.on), 0x42);
    EXPECT_EQ(b.In(0xF0, b.on), Status::DRDY | Status::DSC) << "#F0: status";
    EXPECT_EQ(b.In(0xC8, b.on), Status::DRDY | Status::DSC) << "#C8: alternate status (CS1 register 6)";
    EXPECT_EQ(b.In(0x00, b.on), 0xFF) << "no chip select";
    EXPECT_EQ(b.In(0x18, b.on), 0xFF) << "both chip selects";

    // The board answers only with the TR-DOS ports off, and only for A2 = A1 = 0
    EXPECT_FALSE(b.Claims(0xF0, b.off));
    EXPECT_FALSE(b.Claims(0xFE, b.on));
    EXPECT_FALSE(b.Claims(0x1F, b.on));
    EXPECT_FALSE(b.Claims(0x7FFD, b.on));

    // Word order: OUT (#11),#AB : OUT (#10),#CD sends #ABCD; the image stores CD AB
    StartWrite([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    for (int i = 0; i < 256; i++)
    {
        b.Out(0x11, 0xAB, b.on);
        b.Out(0x10, 0xCD, b.on);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);

    // Read: #10 gives the low byte and latches the high one for #11
    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    EXPECT_EQ(b.In(0x10, b.on), 0x34);
    EXPECT_EQ(b.In(0x11, b.on), 0x12);

    // SRST through #C8
    b.Out(0xC8, DeviceControl::SRST, b.on);
    EXPECT_EQ(b.In(0xF0, b.on), Status::BSY);
    b.Out(0xC8, 0, b.on);
    EXPECT_EQ(b.In(0xF0, b.on), Status::DRDY | Status::DSC);
}

TEST(IdeAdapter_Test, NemoA8LatchesOnA8)
{
    Board b(IDE_NEMO_A8, MM_PENTAGON);
    b.on = Dos(false);
    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    EXPECT_EQ(b.In(0x10, b.on), 0x34);
    EXPECT_EQ(b.In(0x110, b.on), 0x12) << "the latch is at A8 = 1";
    EXPECT_EQ(b.In(0x11, b.on), 0x09) << "A0 is not decoded on Nemo-A8: #11 is the data register again";
}

/// ZX-Evo NemoIDE: every row of tdd-storage-sd-ide-cd.md §3.2
TEST(IdeAdapter_Test, EvoLatchRules)
{
    Board b(IDE_NEMO_DIVIDE, MM_ATM3);
    b.on = Dos(false);
    const IdeAdapter::Gate shadow = Dos(true);

    // Always on: in and out of shadow
    EXPECT_EQ(b.In(0xF0, b.on), b.In(0xF0, shadow));

    // Divide read: INIR over #10 alternates word low / latch, a whole sector in 512 INs
    for (size_t i = 0; i < 512; i++)
        b.disk.Data()[9 * 512 + i] = static_cast<uint8_t>(i);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    for (size_t i = 0; i < 512; i++)
        ASSERT_EQ(b.In(0x10, b.on), static_cast<uint8_t>(i)) << i;
    EXPECT_FALSE(b.Master().State().status & Status::DRQ);

    // IN #11 returns the latch and resets the pair
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    EXPECT_EQ(b.In(0x10, b.on), 0x00);
    EXPECT_EQ(b.In(0x11, b.on), 0x01);
    EXPECT_EQ(b.In(0x10, b.on), 0x02) << "a new word: the pair was reset";

    // Another IDE port resets the pair
    EXPECT_EQ(b.In(0x10, b.on), 0x03) << "the latch half of word 2";
    EXPECT_EQ(b.In(0x10, b.on), 0x04);
    b.In(0xF0, b.on);
    EXPECT_EQ(b.In(0x10, b.on), 0x06) << "status read: the next #10 starts a new word";

    // Nemo order write: #11 then #10
    StartWrite([&](uint8_t reg, uint8_t v) { b.Out(A7A5(reg, 0x10), v, b.on); });
    b.Out(0x11, 0xAB, b.on);
    b.Out(0x10, 0xCD, b.on);
    // Divide order write: #10 low, #10 high
    b.Out(0x10, 0xCD, b.on);
    b.Out(0x10, 0xAB, b.on);
    for (int i = 2; i < 256; i++)
    {
        b.Out(0x11, 0xAB, b.on);
        b.Out(0x10, 0xCD, b.on);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512 + 0], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 2], 0xCD) << "both orders store the same bytes";
    EXPECT_EQ(b.disk.Data()[5 * 512 + 3], 0xAB);

    // RTL aliases: #28 is CS0 register 1, #08 register 0; #18 and #38 are no IDE ports
    b.Out(0x50, 7, b.on);  // sector count
    EXPECT_EQ(b.In(0x48, b.on), 7) << "#48 aliases register 2";
    EXPECT_FALSE(b.Claims(0x18, b.on));
    EXPECT_FALSE(b.Claims(0x38, b.on));
    EXPECT_FALSE(b.Claims(0xFE, b.on));
}

TEST(IdeAdapter_Test, AtmDecodeGateAndIntrq)
{
    Board b(IDE_ATM, MM_ATM710);
    b.on = Dos(true);
    b.off = Dos(false);

    // #xxEF: status, #FExF family: A7..A5 = register, latch at A8 = 1
    EXPECT_EQ(b.In(0xFEEF, b.on), Status::DRDY | Status::DSC);
    EXPECT_FALSE(b.Claims(0xFEEF, b.off)) << "only with the DOS ports on";
    EXPECT_FALSE(b.Claims(0xFE, b.on));

    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(static_cast<uint16_t>(0xFE00 | A7A5(reg, 0x0F)), v, b.on); });
    EXPECT_EQ(b.In(0xFE0F, b.on), 0x34);
    EXPECT_EQ(b.In(0xFF0F, b.on), 0x12);

    // INTRQ on bit 6 of the #7FFD-class read: the command raised it; the status read clears it
    EXPECT_EQ(b.In(0x7FFD, b.off), 0x7F) << "ungated";
    EXPECT_EQ(b.adapter->AtmIntrqBit(), 0x40);
    b.In(0xFEEF, b.on);
    EXPECT_EQ(b.adapter->AtmIntrqBit(), 0x00);

    StartWrite([&](uint8_t reg, uint8_t v) { b.Out(static_cast<uint16_t>(0xFE00 | A7A5(reg, 0x0F)), v, b.on); });
    for (int i = 0; i < 256; i++)
    {
        b.Out(0xFF0F, 0xAB, b.on);
        b.Out(0xFE0F, 0xCD, b.on);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);
}

TEST(IdeAdapter_Test, SmucWindowLatchAndControlBlock)
{
    Board b(IDE_SMUC, MM_PROFSCORP);
    uint8_t system = 0x00;

    // #FFBE: status; #F8BE-#FEBE: registers 0-6 on A10..A8
    EXPECT_EQ(b.adapter->SmucIn(0xFFBE, system), Status::DRDY | Status::DSC);
    b.adapter->SmucOut(A10A8(SectorCount, 0xF8BE), system, 0x42);
    EXPECT_EQ(b.adapter->SmucIn(0xFABE, system), 0x42);

    // #FFBA bit 7 turns #FEBE into the control block
    system = 0x80;
    b.adapter->SmucOut(0xFEBE, system, DeviceControl::SRST);
    EXPECT_EQ(b.adapter->SmucIn(0xFEBE, system), Status::BSY) << "alternate status";
    b.adapter->SmucOut(0xFEBE, system, 0);
    system = 0x00;

    // Data word: #F8BE low byte, #D8BE the latch (Nemo order)
    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.adapter->SmucOut(A10A8(reg, 0xF8BE), system, v); });
    EXPECT_EQ(b.adapter->SmucIn(0xF8BE, system), 0x34);
    EXPECT_EQ(b.adapter->SmucIn(0xD8BE, system), 0x12);
    StartWrite([&](uint8_t reg, uint8_t v) { b.adapter->SmucOut(A10A8(reg, 0xF8BE), system, v); });
    for (int i = 0; i < 256; i++)
    {
        b.adapter->SmucOut(0xD8BE, system, 0xAB);
        b.adapter->SmucOut(0xF8BE, system, 0xCD);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);

    uint8_t value = 0;
    EXPECT_FALSE(b.adapter->In(0xFFBE, Dos(true), value)) << "SMUC goes through the Scorpion decoder";
}

/// Profi: mirrored latch roles (IDE design §3.1, §9)
TEST(IdeAdapter_Test, ProfiMirroredLatches)
{
    Board b(IDE_PROFI, MM_PROFI);
    b.on = ProfiExt(true);
    b.off = ProfiExt(false);

    EXPECT_EQ(b.In(0x07CB, b.on), Status::DRDY | Status::DSC) << "#07CB: status";
    EXPECT_FALSE(b.Claims(0x07CB, b.off)) << "EXT mode only";
    EXPECT_FALSE(b.Claims(0x06AB, b.on)) << "#AB read: nothing drives the bus";
    EXPECT_FALSE(b.Claims(0x008B, b.on));

    // Read: #00CB transfers (low byte), #00EB the latch
    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(A10A8(reg, 0x00EB), v, b.on); });
    EXPECT_EQ(b.In(0x00CB, b.on), 0x34);
    EXPECT_EQ(b.In(0x05EB, b.on), 0x12) << "any A10..A8 reads the latch";

    // Write: #xxCB latches the high byte (any A10..A8), #00EB sends the word
    StartWrite([&](uint8_t reg, uint8_t v) { b.Out(A10A8(reg, 0x00EB), v, b.on); });
    for (int i = 0; i < 256; i++)
    {
        b.Out(0x03CB, 0xAB, b.on);
        b.Out(0x00EB, 0xCD, b.on);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);

    // #06AB: device control
    b.Out(0x06AB, DeviceControl::SRST, b.on);
    EXPECT_EQ(b.In(0x07CB, b.on), Status::BSY);
}

TEST(IdeAdapter_Test, DivideTogglePair)
{
    Board b(IDE_DIVIDE, MM_PENTAGON);
    b.on = Dos(false);
    EXPECT_FALSE(b.Claims(0xBF, Dos(true)));
    EXPECT_EQ(b.In(0xBF, b.on), Status::DRDY | Status::DSC) << "#BF: A4..A2 = 7";

    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.Out(static_cast<uint16_t>(0xA3 | (reg << 2)), v, b.on); });
    EXPECT_EQ(b.In(0xA3, b.on), 0x34);
    EXPECT_EQ(b.In(0xA3, b.on), 0x12);

    StartWrite([&](uint8_t reg, uint8_t v) { b.Out(static_cast<uint16_t>(0xA3 | (reg << 2)), v, b.on); });
    for (int i = 0; i < 256; i++)
    {
        b.Out(0xA3, 0xCD, b.on);
        b.Out(0xA3, 0xAB, b.on);
    }
    EXPECT_EQ(b.disk.Data()[5 * 512], 0xCD);
    EXPECT_EQ(b.disk.Data()[5 * 512 + 1], 0xAB);
}

/// No board, or no unit: nothing is decoded; the machine keeps its own ports
TEST(IdeAdapter_Test, NoBoardNoDecode)
{
    EmulatorContext context(LoggerLevel::LogError);
    IdeAdapter adapter(&context);
    uint8_t value = 0;
    EXPECT_FALSE(adapter.In(0xF0, Dos(false), value));
    EXPECT_FALSE(adapter.Out(0xF0, Dos(false), 0));
    EXPECT_FALSE(adapter.Active());
}

/// Robustness (IDE design §12.7): random port I/O on every board never crashes,
/// never writes past the medium, and the state stays plain data that copies
/// exactly (a TTD restore in the middle of anything)
TEST(IdeAdapter_Test, RandomPortTrafficIsSafe)
{
    for (IDE_SCHEME scheme : {IDE_NEMO, IDE_NEMO_A8, IDE_NEMO_DIVIDE, IDE_ATM, IDE_PROFI, IDE_DIVIDE, IDE_SMUC})
    {
        const MEM_MODEL model = scheme == IDE_PROFI ? MM_PROFI : scheme == IDE_ATM ? MM_ATM710
                                : scheme == IDE_SMUC ? MM_PROFSCORP : MM_PENTAGON;
        Board b(scheme, model);
        uint32_t seed = 0x1DE5EEDu + static_cast<uint32_t>(scheme);
        auto next = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        for (int i = 0; i < 20000; i++)
        {
            const uint32_t r = next();
            // Mostly the board's own port families, sometimes anything
            uint16_t port = static_cast<uint16_t>(next());
            if ((r & 3) != 0)
            {
                switch (scheme)
                {
                    case IDE_PROFI: port = static_cast<uint16_t>((port & 0x0700) | 0x8B | (port & 0x60)); break;
                    case IDE_ATM: port = static_cast<uint16_t>((port & 0x01E0) | 0xFE0F); break;
                    case IDE_DIVIDE: port = static_cast<uint16_t>(0xA3 | (port & 0x1C)); break;
                    case IDE_SMUC: port = static_cast<uint16_t>(0xD8BE | (port & 0x2700)); break;
                    default: port = static_cast<uint16_t>(port & 0xF9); break;  // A2 = A1 = 0
                }
            }
            IdeAdapter::Gate gate;
            gate.dosPorts = (r >> 2) & 1;
            gate.profiExt = (r >> 3) & 1;
            uint8_t value = static_cast<uint8_t>(next());
            if (scheme == IDE_SMUC)
            {
                const uint8_t system = (r >> 4) & 1 ? 0x80 : 0x00;
                if ((r >> 5) & 1)
                    b.adapter->SmucOut(port, system, value);
                else
                    b.adapter->SmucIn(port, system);
            }
            else if ((r >> 5) & 1)
                b.adapter->Out(port, gate, value);
            else
                b.adapter->In(port, gate, value);
        }

        // Copy the whole board state into a fresh board: the same bytes
        Board copy(scheme, model);
        copy.adapter->SetState(b.adapter->State());
        copy.ide->Channel().SetSelectedState(b.ide->Channel().SelectedState());
        copy.Master().SetState(b.Master().State());
        EXPECT_EQ(std::memcmp(&copy.Master().State(), &b.Master().State(), sizeof(AtaDeviceState)), 0);
        EXPECT_EQ(std::memcmp(&copy.adapter->State(), &b.adapter->State(), sizeof(IdeAdapterState)), 0);
        EXPECT_EQ(b.disk.SectorCount(), 256u) << Config::IdeSchemeName(scheme);
    }
}

/// SMUC: #D8BE is the latch whatever #FFBA bit 7 says (MAME, Xpeccy); only the
/// window turns into the control block
TEST(IdeAdapter_Test, SmucLatchIgnoresTheControlBlockBit)
{
    Board b(IDE_SMUC, MM_PROFSCORP);
    b.adapter->SmucOut(0xD8BE, 0x80, 0xAB);
    EXPECT_EQ(b.Master().State().control, 0) << "the latch write reached no register";
    std::memcpy(b.disk.Data() + 9 * 512, "\x34\x12", 2);
    StartRead([&](uint8_t reg, uint8_t v) { b.adapter->SmucOut(A10A8(reg, 0xF8BE), 0x00, v); });
    EXPECT_EQ(b.adapter->SmucIn(0xF8BE, 0x00), 0x34);
    EXPECT_EQ(b.adapter->SmucIn(0xD8BE, 0x80), 0x12) << "the latch, not alternate status";
}

/// DivIDE: #E3 / #E7 / #EB are its paging ports, never IDE registers
TEST(IdeAdapter_Test, DividePagingPortsAreNotIde)
{
    Board b(IDE_DIVIDE, MM_PENTAGON);
    for (uint16_t port : {0x00E3, 0x00E7, 0x00EB, 0x00EF})
        EXPECT_FALSE(b.Claims(port, Dos(false))) << std::hex << port;
    EXPECT_TRUE(b.Claims(0x00A3, Dos(false)));
    EXPECT_TRUE(b.Claims(0x00BF, Dos(false)));
}
