// ZX Spectrum Next: the real boot chain on the emulated Z80 - the boot ROM from the FPGA sources loads TBBLUE.FW
// from a FAT16 card over SPI and jumps to #6000. The firmware is the SpecNext team's, so it is not in the
// repository: point UNREAL_NEXT_FIRMWARE at a folder with the card layout (TBBLUE.FW and machines/next/...);
// without it the tests skip. Design: design-boot-and-firmware.md section 2, research-fpga-vhdl.md section 14.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextFirmware_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        const char* folder = std::getenv("UNREAL_NEXT_FIRMWARE");
        if (!folder || !std::filesystem::exists(std::filesystem::path(folder) / "TBBLUE.FW"))
            GTEST_SKIP() << "UNREAL_NEXT_FIRMWARE does not point to a folder with TBBLUE.FW";

        _emulator = EmulatorTestHelper::CreateStandardEmulator(
            "NEXT", LoggerLevel::LogError, RamPowerOn::Zero,
            [](CONFIG& config) { std::strncpy(config.next_boot_rom_path, "rom/next/nextboot.rom", sizeof config.next_boot_rom_path - 1); });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_TRUE(_memory && _ports && _memory->HasBootRom());
        _context->emulatorState.p7FFD = 0;
        _memory->UpdateZ80Banks();

        FolderSnapshot snapshot;
        FolderScanOptions scan;
        std::string error;
        ASSERT_TRUE(FolderSnapshot::Scan(folder, scan, snapshot, &error)) << error;
        FatVolumeOptions options;
        std::vector<std::string> report;
        auto card = HostFolderFat::Build(snapshot, options, &error, &report);
        ASSERT_NE(card, nullptr) << error;
        ASSERT_TRUE(_ports->InsertSdCard(0, std::move(card), SdCardSpi::WriteMode::Session));
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

namespace
{
std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

// The boot ROM initializes the card over SPI, reads TBBLUE.FW's index, copies the boot module to #6000 and jumps
// to it (boot ROM off by the module's NR #03 write). ~100 blocks read by then
TEST_F(NextFirmware_Test, BootRomLoadsTheFirmwareAndJumpsTo6000)
{
    int frames = 0;
    for (; frames < 400; frames++)
    {
        _emulator->RunFrame(true);
        if (_z80->pc >= 0x6000 && _z80->pc < 0x8000)
            break;
    }
    EXPECT_LT(frames, 400);
    EXPECT_GT(_ports->SdCard(0).blocksRead(), 20u);
}

// The golden sequence of research-fpga-vhdl.md section 14 / design-boot-and-firmware.md section 2, as the real
// firmware produces it on this machine: NR #07 = 3 first, the boot ROM off by NR #03, the ROMs written through the
// NR #04 config mapping, the settings and the +3 port enables, NR #03 = machine type, NR #02 = soft reset - and the
// personality ROM then runs from the system area the firmware filled
TEST_F(NextFirmware_Test, FirmwareWritesTheRegistersAndSoftResetsIntoThePersonality)
{
    std::vector<NextRegWrite> log;
    uint16_t pcMirror = 0;
    _ports->Board().SetWriteLog(&log, &pcMirror);
    auto softResets = [&]() {
        size_t n = 0;
        for (const NextRegWrite& w : log)
            n += (w.reg == 0x02 && (w.value & 3)) ? 1 : 0;
        return n;
    };
    int frames = 0;
    for (; frames < 3000 && softResets() == 0; frames++)
    {
        _emulator->RunFrame(true);
        pcMirror = _z80->pc;
    }
    ASSERT_EQ(softResets(), 1u) << "no soft reset after " << frames << " frames";

    // The registers outside the palette / keymap / video setup, in order
    std::vector<std::pair<uint8_t, uint8_t>> sequence;
    for (const NextRegWrite& w : log)
        if (w.reg <= 0x0A || (w.reg >= 0x80 && w.reg <= 0x85))
            if (w.reg != 0x04)
                sequence.push_back({w.reg, w.value});
    ASSERT_GE(sequence.size(), 14u);
    EXPECT_EQ(sequence[0], (std::pair<uint8_t, uint8_t>{0x07, 0x03})) << "28 MHz for the boot";
    const std::vector<std::pair<uint8_t, uint8_t>> tail = {{0x05, 0x71}, {0x06, 0x9D}, {0x08, 0x1E}, {0x09, 0x00},
                                                          {0x0A, 0x11}, {0x82, 0xDA}, {0x83, 0x3F}, {0x84, 0xFF},
                                                          {0x85, 0x01}, {0x03, 0xB3}, {0x02, 0x01}};
    // the first 11 entries of the tail are the init_registers / machine type / reset steps, the last in the log
    // before the reset
    size_t resetAt = 0;
    for (size_t i = 0; i < sequence.size(); i++)
        if (sequence[i].first == 0x02 && (sequence[i].second & 3))
            resetAt = i;
    ASSERT_GE(resetAt, tail.size());
    for (size_t i = 0; i < tail.size(); i++)
        EXPECT_EQ(sequence[resetAt + 1 - tail.size() + i], tail[i]) << "step " << i;
    bool bootRomOff = false;
    for (const NextRegWrite& w : log)
        bootRomOff |= w.reg == 0x03 && w.value == 0x00;
    EXPECT_TRUE(bootRomOff) << "NR #03 = 0 switches the boot ROM off before the ROMs are loaded";

    // Run on: the personality starts from the system area, out of config mode, +3 machine type
    for (int i = 0; i < 10; i++)
        _emulator->RunFrame(true);
    EXPECT_FALSE(_memory->InConfigMode());
    EXPECT_FALSE(_memory->BootRomEnabled());
    EXPECT_EQ(_ports->Board().MachineType(), 3);
    const std::vector<uint8_t> rom = ReadFile(std::filesystem::path(std::getenv("UNREAL_NEXT_FIRMWARE")) / "machines/next/enNextZX.rom");
    ASSERT_EQ(rom.size(), 65536u);
    EXPECT_EQ(std::memcmp(_memory->ROMPageHostAddress(0), rom.data(), 4 * 16384), 0) << "the personality ROM is in the system area";
    const std::vector<uint8_t> divmmc = ReadFile(std::filesystem::path(std::getenv("UNREAL_NEXT_FIRMWARE")) / "machines/next/enNxtmmc.rom");
    ASSERT_EQ(divmmc.size(), 8192u);
    // DivMMC ROM: SRAM #010000 = the first half of ROM page 4
    EXPECT_EQ(std::memcmp(_memory->ROMPageHostAddress(4), divmmc.data(), 8192), 0) << "the DivMMC ROM";
}

// Bring-up: run the personality ROM after the firmware's soft reset and list what it asks of the machine. Not a
// gate (prints to stderr): UNREAL_NEXT_HUNT=<frames> runs it
TEST_F(NextFirmware_Test, PersonalityRomStallHunt)
{
    const char* hunt = std::getenv("UNREAL_NEXT_HUNT");
    if (!hunt)
        GTEST_SKIP() << "UNREAL_NEXT_HUNT not set";
    std::vector<NextRegWrite> log;
    uint16_t pcMirror = 0;
    std::map<uint8_t, uint32_t> nrReads;
    std::map<uint16_t, PortDecoder_Next::PortUse> inLog, outLog;
    _ports->Board().SetWriteLog(&log, &pcMirror);
    _ports->Board().SetReadCounts(&nrReads);
    _ports->SetPortLog(&inLog, &outLog);
    int resetAt = -1;
    const int total = std::atoi(hunt);
    std::map<uint16_t, uint32_t> pcHist;
    for (int f = 0; f < total; f++)
    {
        _emulator->RunFrame(true);
        pcMirror = _z80->pc;
        if (resetAt < 0)
            for (const NextRegWrite& w : log)
                if (w.reg == 0x02 && (w.value & 3))
                    resetAt = f;
        if (resetAt >= 0)
            pcHist[_z80->pc & 0xFFF0]++;
    }
    std::fprintf(stderr, "soft reset at frame %d of %d, pc=%04X sp=%04X iff=%d im=%d halted=%d mmu:", resetAt, total, _z80->pc, _z80->sp,
                 _z80->iff1, _z80->im, _z80->halted);
    for (unsigned i = 0; i < 8; i++)
        std::fprintf(stderr, " %02X", _memory->GetMmu(i));
    std::fprintf(stderr, " rom=%d type=%d bytes@pc-8:", _memory->GetRomSelect(), _ports->Board().MachineType());
    for (int k = -8; k < 16; k++)
        std::fprintf(stderr, " %02X", _memory->PeekSlot(static_cast<uint16_t>(_z80->pc + k)));
    std::fprintf(stderr, "\nNR reads:");
    for (auto& [r, n] : nrReads)
        std::fprintf(stderr, " %02X x%u", r, n);
    std::fprintf(stderr, "\nunhandled IN:");
    for (auto& [p, u] : inLog)
        std::fprintf(stderr, " %04X x%u(=%02X)", p, u.count, u.last);
    std::fprintf(stderr, "\nunhandled OUT:");
    for (auto& [p, u] : outLog)
        std::fprintf(stderr, " %04X x%u(=%02X)", p, u.count, u.last);
    std::fprintf(stderr, "\nNR writes after reset (distinct):");
    std::map<uint16_t, uint32_t> wr;
    bool after = false;
    for (const NextRegWrite& w : log)
    {
        if (w.reg == 0x02 && (w.value & 3))
            after = true;
        else if (after)
            wr[static_cast<uint16_t>(w.reg << 8 | w.value)]++;
    }
    for (auto& [k, n] : wr)
        std::fprintf(stderr, " %02X=%02X x%u", k >> 8, k & 0xFF, n);
    std::fprintf(stderr, "\npc histogram (top):");
    std::vector<std::pair<uint32_t, uint16_t>> top;
    for (auto& [p, n] : pcHist)
        top.push_back({n, p});
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 8; i++)
        std::fprintf(stderr, " %04X x%u", top[i].second, top[i].first);
    std::fprintf(stderr, "\n");
}

// Where does the personality ROM restart from? Single-steps after the soft reset and reports the instructions that
// landed on #0000 (UNREAL_NEXT_HUNT set)
TEST_F(NextFirmware_Test, PersonalityRomRestartHunt)
{
    if (!std::getenv("UNREAL_NEXT_HUNT"))
        GTEST_SKIP() << "UNREAL_NEXT_HUNT not set";
    std::vector<NextRegWrite> log;
    uint16_t pcMirror = 0;
    _ports->Board().SetWriteLog(&log, &pcMirror);
    for (int f = 0; f < 3000; f++)
    {
        _emulator->RunFrame(true);
        pcMirror = _z80->pc;
        bool reset = false;
        for (const NextRegWrite& w : log)
            reset |= w.reg == 0x02 && (w.value & 3);
        if (reset)
            break;
    }
    // now inside the personality: report every arrival in the early init (#0000-#013F) from far away
    int arrivals = 0;
    uint16_t prev = _z80->pc;
    for (long i = 0; i < 12'000'000 && arrivals < 8; i++)
    {
        _z80->EngineStep();
        if (_z80->pc < 0x0140 && prev >= 0x0200 && (_z80->pc < 0x08 || _z80->pc > 0x40))
        {
            std::fprintf(stderr, "arrival #%04X from #%04X step %ld sp=%04X ret=%02X%02X rom=%d mmu0=%02X mmu1=%02X bytes@prev:", _z80->pc, prev, i,
                         _z80->sp, _memory->PeekSlot(static_cast<uint16_t>(_z80->sp + 1)), _memory->PeekSlot(_z80->sp), _memory->GetRomSelect(),
                         _memory->GetMmu(0), _memory->GetMmu(1));
            for (int k = 0; k < 4; k++)
                std::fprintf(stderr, " %02X", _memory->PeekSlot(static_cast<uint16_t>(prev + k)));
            std::fprintf(stderr, "\n");
            arrivals++;
        }
        prev = _z80->pc;
    }
}
