#pragma once

#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/platform.h"

/// region <Synthetic memory image tags>

// Self-identifying fill bytes: any read through a Z80-visible window answers
// "which page is mapped here?" without touching Memory internals. The scheme is
// shared by every Profi test (derived from the Scorpion fixture scheme):
//   RAM page n (0-63)  -> 0x40 | n   (0x40-0x7F)
//   ROM page k (0-63)  -> 0xC0 | k   (0xC0-0xFF)
// The synthetic 64 KB bundle follows the Profi ROM order SYS / TR-DOS / 128K / 48K
// (tags 0xC0..0xC3), see docs/inprogress/2026-09-21-profi/technical-design.md section 4.
constexpr uint8_t ProfiRamTagBase = 0x40;
constexpr uint8_t ProfiRomTagBase = 0xC0;
constexpr uint8_t ProfiTaggedRamPages = 64;  // tags distinguish pages 0-63 only

constexpr bool ProfiIsRamTag(uint8_t tag)
{
    return (tag & 0xC0) == ProfiRamTagBase;
}

constexpr bool ProfiIsRomTag(uint8_t tag)
{
    return (tag & 0xC0) == ProfiRomTagBase;
}

constexpr uint8_t ProfiTagPage(uint8_t tag)
{
    return static_cast<uint8_t>(tag & 0x3F);
}

/// endregion </Synthetic memory image tags>

/// @brief Full-machine test fixture for the ZX Profi 1024.
///
/// Builds a real EmulatorContext with Memory, Keyboard, Screen, WD1793 and the
/// model's PortDecoder wired by Core::Init(), then loads a synthetic patterned
/// ROM bundle (instead of a real image) so every test is hermetic and
/// deterministic. RAM pages carry self-identifying fill bytes, so bank-map
/// assertions reduce to a single read through the Z80-visible window.
///
/// Mirrors the context-building pattern of emulator_test.h /
/// scorpionrommapping_test.cpp: Core::Init() only instantiates the ROM object,
/// the file load is the fixture's job (Emulator::Init() does the same).
class ProfiMachineFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Memory* _memory = nullptr;
    std::string _romPath;

    // Machine shape - change via RebuildWithModel() / LoadSyntheticRom() from a test body
    MEM_MODEL _model = MM_PROFI;
    uint32_t _ramSizeKB = RAM_1024;
    uint16_t _romQuadrants = 1;

    void SetUp() override
    {
        if (!BuildMachine())
        {
            FAIL() << "ProfiMachineFixture: machine construction failed";
        }
    }

    void TearDown() override
    {
        DestroyMachine();
    }

    /// @brief Tear the machine down and rebuild it with a different model / RAM size.
    /// @return false when the rebuilt machine could not be constructed
    bool RebuildWithModel(MEM_MODEL model, uint32_t ramSizeKB)
    {
        DestroyMachine();
        _model = model;
        _ramSizeKB = ramSizeKB;
        return BuildMachine();
    }

    /// @brief Regenerate the synthetic bundle with the given number of 64 KB quadrants
    ///        and reload it (ProfROM ladder tests). RAM patterns are re-applied.
    /// @return false when the reload failed (e.g. the model rejects the size)
    bool LoadSyntheticRom(uint16_t quadrants)
    {
        _romQuadrants = quadrants;
        if (!WritePatternedRomBundle(_romPath, _romQuadrants))
            return false;

        ROM rom(_context);
        if (!rom.LoadROM())
            return false;

        _context->pPortDecoder->reset();
        PatternFillRam();
        return true;
    }

    /// region <Drive helpers>

    void WritePort(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->DecodePortOut(port, value, 0x0000);
    }

    uint8_t ReadPort(uint16_t port)
    {
        return _context->pPortDecoder->DecodePortIn(port, 0x0000);
    }

    /// @brief Read the self-identifying fill byte through the Z80-visible window.
    ///        ProfiIsRamTag()/ProfiTagPage() turn it into a page id.
    uint8_t BankTag(uint16_t window)
    {
        return _memory->DirectReadFromZ80Memory(window);
    }

    /// @brief Raw write into the currently mapped page behind a Z80 window
    ///        (writes to ROM windows land in the trash page by design).
    void DirectWrite(uint16_t address, uint8_t value)
    {
        _memory->DirectWriteToZ80Memory(address, value);
    }

    /// @brief Execute Z80 opcodes until the given number of t-states elapses.
    ///        The step guard keeps a runaway loop (page filled with JP opcodes)
    ///        from hanging a shard.
    void RunTStates(uint32_t tstates)
    {
        Z80* z80 = _core->GetZ80();
        const uint32_t target = z80->t + tstates;
        uint64_t steps = 0;
        while (z80->t < target && steps < 10'000'000)
        {
            z80->Z80Step();
            steps++;
        }
    }

    void RunFrames(uint32_t frames)
    {
        RunTStates(frames * _context->config.frame);
    }

    /// endregion </Drive helpers>

private:
    bool BuildMachine()
    {
        if (_model != MM_PROFI)
        {
            return false;
        }

        _context = new EmulatorContext(LoggerLevel::LogError);

        CONFIG& config = _context->config;
        config.mem_model = _model;
        config.ramsize = _ramSizeKB;
        config.trdos_present = true;  // Beta128 is part of the Profi configuration ([BETA128] Beta128=1)

        // Raster geometry. Emulator::Init() normally does this before it
        // constructs Core; a fixture that builds Core directly has to do it
        // itself, and CONFIG{} leaves config.frame at 0. Screen requires it:
        // InitRaster asserts config.frame >= the raster's own frame length
        // (#ifdef _DEBUG), and SetVideoMode pushes the value into
        // UlaContention, where GetIOContentionDelay computes
        // `t % configFrameDuration` - a modulo by zero on any model whose
        // contention is enabled. Go through ApplyModelTimingDefaults rather
        // than a literal so the fixture cannot drift from the model.
        Config configHelper(_context);
        configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

        // ROM bundle must exist before Core::Init() only in the sense that the
        // loader runs right after - the config path is what matters here
        _romPath = TestPathHelper::GetUniqueTestScratchPath("profifixture.rom");
        if (!WritePatternedRomBundle(_romPath, _romQuadrants))
            return false;

        CopyRomPath(config.profi_rom_path, _romPath);

        _core = new Core(_context);
        if (!_core->Init())
            return false;

        _memory = _context->pMemory;

        // Core::Init() instantiates the ROM object but does not load any file;
        // Emulator::Init() does that separately and so does the fixture
        ROM rom(_context);
        if (!rom.LoadROM())
            return false;

        // Establish the model's power-on bank map (fixed banks, boot ROM, port latches)
        _context->pPortDecoder->reset();

        PatternFillRam();

        return true;
    }

    void DestroyMachine()
    {
        // Core::~Core() -> Release() frees every peripheral (decoder last) and
        // nulls the context back-pointers, mirroring Emulator_Test::DestroyEmulator()
        if (_core != nullptr)
        {
            delete _core;
            _core = nullptr;
        }

        if (_context != nullptr)
        {
            delete _context;
            _context = nullptr;
        }

        _memory = nullptr;

        if (!_romPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(_romPath, ec);
        }
    }

    void PatternFillRam()
    {
        for (uint16_t page = 0; page < ProfiTaggedRamPages; page++)
        {
            std::memset(_memory->RAMBase() + page * PAGE_SIZE,
                        static_cast<uint8_t>(ProfiRamTagBase | page),
                        PAGE_SIZE);
        }
    }

    static void CopyRomPath(char (&destination)[FILENAME_MAX], const std::string& path)
    {
        std::strncpy(destination, path.c_str(), sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
    }

    /// @brief Emit a patterned ROM bundle: every 16 KB page filled with its own tag
    static bool WritePatternedRomBundle(const std::string& path, uint16_t quadrants)
    {
        const uint16_t pages = static_cast<uint16_t>(4 * quadrants);
        std::vector<uint8_t> image(static_cast<size_t>(pages) * PAGE_SIZE);
        for (uint16_t page = 0; page < pages; page++)
        {
            std::fill_n(image.begin() + static_cast<size_t>(page) * PAGE_SIZE,
                        PAGE_SIZE,
                        static_cast<uint8_t>(ProfiRomTagBase | page));
        }
        return FileHelper::SaveBufferToFile(path, image.data(), image.size());
    }
};
