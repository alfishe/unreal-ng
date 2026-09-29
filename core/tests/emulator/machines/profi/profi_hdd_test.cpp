// Profi IDE on the real firmware (IDE design §3.6, §12.5 R1 / R2): the SYS
// ROM's HDD boot loader at #28CE of page 0, run against the Profi board
// (EXT mode gate, mirrored latches, CHS, EXECUTE DIAGNOSTIC / RECALIBRATE /
// READ SECTORS without retry, SRST through #06AB).
//
// Needs the original ROM, testdata/machines/profi/rom/profi_mainrom_standart.rom
// (not shipped: the shipped profi.rom has no HDD loader); skipped without it.
// Real-ROM runs: slower than 50 ms by nature

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// The disk image as the Profi BIOS writes it: every word is stored high
    /// byte first in memory, so the image holds memory bytes pairwise swapped
    void PutProfiOrder(std::string& image, uint64_t lba, const std::vector<uint8_t>& memory)
    {
        for (size_t i = 0; i < memory.size(); i++)
            image[lba * 512 + (i ^ 1)] = static_cast<char>(memory[i]);
    }

    class ProfiHdd_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;

        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        /// The Profi with the original ROM; a disk without a ProfiHiDD header
        /// gets the SYS ROM's format (16 heads x 16 sectors) by itself. False
        /// when the ROM is not here
        bool Create()
        {
            const std::filesystem::path rom =
                TestPathHelper::FindProjectRoot() / "testdata/machines/profi/rom/profi_mainrom_standart.rom";
            if (!std::filesystem::exists(rom))
                return false;
            _emulator = EmulatorTestHelper::CreateStandardEmulator("PROFI", LoggerLevel::LogError);
            EXPECT_NE(_emulator, nullptr);
            if (!_emulator)
                return false;
            _context = _emulator->GetContext();
            std::strncpy(_context->config.profi_rom_path, Utf8(rom).c_str(), sizeof(_context->config.profi_rom_path) - 1);
            EXPECT_TRUE(_context->pCore->GetROM()->LoadROM());
            _emulator->Reset();
            return true;
        }

        void EnterLoader()
        {
            Memory& memory = *_context->pMemory;
            // The SYS ROM (file page 0) at #0000: the DOS latch on, ROM14 off
            memory.SetROMMode(RM_SYS);
            const char expected[] = "HDD error";
            for (size_t i = 0; i < sizeof(expected) - 1; i++)
                ASSERT_EQ(memory.DirectReadFromZ80Memory(static_cast<uint16_t>(0x28B3 + i)), static_cast<uint8_t>(expected[i]))
                    << "the SYS ROM (page 0) is not at #0000";
            _context->pCore->GetZ80()->pc = 0x28CE;
        }

        bool RunUntilPc(uint16_t pc, unsigned frames)
        {
            Z80* z80 = _context->pCore->GetZ80();
            EmulatorTestHelper::RunUntil(_emulator, [&] { return z80->pc == pc; }, frames);
            return z80->pc == pc;
        }
    };
}  // namespace

/// R1: the loader reads C1/H0/S1 (heads, sectors per track), then C1/H0/S6
/// (a sector count and the program) to #00FE, then that many sectors from
/// C1/H0/S7 to #02FE, and jumps to #0100
TEST_F(ProfiHdd_Test, SysRomBootsFromTheHardDisk)
{
    if (!Create())
        GTEST_SKIP() << "profi_mainrom_standart.rom is not in testdata";

    // 1024 sectors, 16 x 16: C1/H0/Sn is LBA 256 + n - 1
    std::string disk(1024 * 512, '\0');
    PutProfiOrder(disk, 256, {0x00, 4, 0x00, 16});  // #0100: 4 heads, #0102: 16 sectors per track
    // S6 lands at #00FE: the count (1), then at #0100: LD HL,#C0DE : LD (#9000),HL : JR $
    PutProfiOrder(disk, 261, {1, 0, 0x21, 0xDE, 0xC0, 0x22, 0x00, 0x90, 0x18, 0xFE});
    PutProfiOrder(disk, 262, {'P', 'R', 'O', 'F', 'I', 'H', 'D', 'D'});  // S7 -> #02FE
    ScratchFolder folder("profi-hdd");
    MediaSource source;
    source.path = Utf8(folder.File("profi.img", disk));
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.master", source, options).Ok());

    EnterLoader();
    ASSERT_TRUE(RunUntilPc(0x0106, 200)) << "the program at #0100 did not run, pc=" << std::hex << _context->pCore->GetZ80()->pc;
    Memory& memory = *_context->pMemory;
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9000), 0xDE);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9001), 0xC0);
    const char marker[] = "PROFIHDD";
    for (size_t i = 0; i < 8; i++)
        EXPECT_EQ(memory.DirectReadFromZ80Memory(static_cast<uint16_t>(0x02FE + i)), static_cast<uint8_t>(marker[i])) << i;
}

/// R2: with no drive the status reads #FF (busy forever), the loader times
/// out, fills #C000-#FFFF with #C2 and parks at #80A4
TEST_F(ProfiHdd_Test, NoDriveEndsInTheErrorExit)
{
    if (!Create())
        GTEST_SKIP() << "profi_mainrom_standart.rom is not in testdata";

    EnterLoader();
    ASSERT_TRUE(RunUntilPc(0x80A4, 5000)) << "pc=" << std::hex << _context->pCore->GetZ80()->pc;
    Memory& memory = *_context->pMemory;
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xC000), 0xC2);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xFFFF), 0xC2);
}
